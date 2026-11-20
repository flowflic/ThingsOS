/* SPDX-License-Identifier: Apache-2.0 */
#include "network_probe.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/etharp.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/tcpip.h"
#include "ping/ping_sock.h"

static const char *TAG="ThingsOS_probe";
static SemaphoreHandle_t completed;
static uint32_t interface_index;
static const char *ping_label;
static void ping_reply(esp_ping_handle_t h, void *arg) {
    uint32_t ms=0;
    esp_ping_get_profile(h, ESP_PING_PROF_TIMEGAP, &ms, sizeof(ms));
    ESP_LOGI(TAG,"PING %s reply time=%lu ms",ping_label,(unsigned long)ms);
}
static void ping_timeout(esp_ping_handle_t h, void *arg) {
    ESP_LOGW(TAG,"PING %s timeout",ping_label);
}
static void ping_end(esp_ping_handle_t h, void *arg) {
    uint32_t sent=0,received=0;
    esp_ping_get_profile(h,ESP_PING_PROF_REQUEST,&sent,sizeof(sent));
    esp_ping_get_profile(h,ESP_PING_PROF_REPLY,&received,sizeof(received));
    ESP_LOGI(TAG,"PING %s sent=%lu received=%lu",ping_label,(unsigned long)sent,(unsigned long)received);
    xSemaphoreGive(completed);
}
static void ping_address(const char *label,uint32_t address) {
    esp_ping_config_t cfg=ESP_PING_DEFAULT_CONFIG();
    cfg.count=3;cfg.interface=interface_index;
    IP_ADDR4(&cfg.target_addr,0,0,0,0);
    ip_2_ip4(&cfg.target_addr)->addr=address;
    esp_ping_callbacks_t cb={.on_ping_success=ping_reply,.on_ping_timeout=ping_timeout,.on_ping_end=ping_end};
    esp_ping_handle_t h=NULL;ping_label=label;
    esp_err_t rc=esp_ping_new_session(&cfg,&cb,&h);
    if(rc!=ESP_OK){ESP_LOGE(TAG,"PING %s create=%s",label,esp_err_to_name(rc));return;}
    rc=esp_ping_start(h);
    if(rc==ESP_OK) xSemaphoreTake(completed,portMAX_DELAY);
    esp_ping_delete_session(h);
    vTaskDelay(pdMS_TO_TICKS(100));
}
static void arp_dump(void *arg) {
    int count=0;
    for(int i=0;i<ARP_TABLE_SIZE;i++) {
        ip4_addr_t *ip=NULL;struct netif *n=NULL;struct eth_addr *mac=NULL;
        if(etharp_get_entry(i,&ip,&n,&mac)) {
            ESP_LOGI(TAG,"ARP %s via %c%c%u mac=%02x:%02x:%02x:%02x:%02x:%02x",ip4addr_ntoa(ip),n->name[0],n->name[1],n->num,
                     mac->addr[0],mac->addr[1],mac->addr[2],mac->addr[3],mac->addr[4],mac->addr[5]);count++;
        }
    }
    ESP_LOGI(TAG,"ARP stable entries=%d",count);

    xSemaphoreGive(completed);
}
static void dump_arp(void) {
    if(tcpip_callback(arp_dump,NULL)==ERR_OK)xSemaphoreTake(completed,portMAX_DELAY);
}
static bool resolve_host(const char *name,bool ping) {
    struct addrinfo hints={.ai_family=AF_INET,.ai_socktype=SOCK_STREAM},*answer=NULL;
    int64_t start=esp_timer_get_time();
    int rc=getaddrinfo(name,NULL,&hints,&answer);
    ESP_LOGI(TAG,"DNS %s result=%d elapsed=%lld ms",name,rc,(long long)((esp_timer_get_time()-start)/1000));
    if(rc==0 && answer) {
        struct sockaddr_in *addr=(struct sockaddr_in *)answer->ai_addr;
        ESP_LOGI(TAG,"DNS %s address=%s",name,inet_ntoa(addr->sin_addr));
        if(ping)ping_address(name,addr->sin_addr.s_addr);
    }
    if(answer)freeaddrinfo(answer);
    return rc==0;
}
static void tcp_public(void) {
    int fd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(fd<0){ESP_LOGW(TAG,"TCP socket errno=%d",errno);return;}
    fcntl(fd,F_SETFL,O_NONBLOCK);
    struct sockaddr_in peer={.sin_family=AF_INET,.sin_port=htons(443)};
    inet_pton(AF_INET,"124.223.145.201",&peer.sin_addr);
    int64_t start=esp_timer_get_time();
    int rc=connect(fd,(struct sockaddr *)&peer,sizeof(peer));int error=errno;
    if(rc<0 && error==EINPROGRESS) {
        fd_set writes;FD_ZERO(&writes);FD_SET(fd,&writes);
        struct timeval timeout={.tv_sec=5};
        int ready=select(fd+1,NULL,&writes,NULL,&timeout);
        if(ready>0){socklen_t size=sizeof(error);getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size);rc=error?-1:0;}
        else {rc=-1;error=ready==0?ETIMEDOUT:errno;}
    }
    ESP_LOGI(TAG,"TCP 124.223.145.201:443 result=%d errno=%d elapsed=%lld ms",rc,rc<0?error:0,(long long)((esp_timer_get_time()-start)/1000));
    close(fd);
}
static void probe_task(void *arg) {
    /* Let initial panel power and startup health settle before network testing. */
    vTaskDelay(pdMS_TO_TICKS(35000));
    esp_netif_t *sta=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t info={0};
    if(!sta || esp_netif_get_ip_info(sta,&info)!=ESP_OK || !info.ip.addr) {
        ESP_LOGW(TAG,"Probe skipped: STA has no IPv4 address");goto done;
    }
    interface_index=esp_netif_get_netif_impl_index(sta);
    ESP_LOGI(TAG,"BEGIN STA ip="IPSTR" gateway="IPSTR" interface=%lu",IP2STR(&info.ip),IP2STR(&info.gw),(unsigned long)interface_index);
    for(int i=0;i<3;i++) {
        esp_netif_dns_info_t dns={0};
        if(esp_netif_get_dns_info(sta,i,&dns)==ESP_OK)ESP_LOGI(TAG,"DNS server[%d]=%s",i,ipaddr_ntoa((ip_addr_t *)&dns.ip));
    }
    ping_address("gateway",info.gw.addr);dump_arp();
    struct in_addr public_ip;inet_pton(AF_INET,"223.5.5.5",&public_ip);
    ping_address("223.5.5.5",public_ip.s_addr);tcp_public();
    resolve_host("baidu.com",true);resolve_host("weread.qq.com",false);dump_arp();
    ESP_LOGI(TAG,"END layered network probe");
done:
    vSemaphoreDelete(completed);completed=NULL;vTaskDelete(NULL);
}
void thingsos_network_probe_start(void) {
    if(completed)return;
    completed=xSemaphoreCreateBinary();
    if(completed && xTaskCreate(probe_task,"net_probe",8192,NULL,2,NULL)!=pdPASS) {
        vSemaphoreDelete(completed);completed=NULL;
    }
}

