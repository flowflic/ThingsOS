// SPDX-License-Identifier: Apache-2.0
// Native dialog supports keyboard focus, Escape and focus restoration.
const preview=document.getElementById('screen-dialog'),large=document.getElementById('large-screen');
document.querySelectorAll('[data-screen]').forEach(button=>button.addEventListener('click',()=>{
 large.src=button.dataset.screen;large.alt=button.querySelector('img').alt;
 document.getElementById('screen-caption').textContent=button.dataset.caption;preview.showModal();
}));
document.getElementById('close-screen').addEventListener('click',()=>preview.close());
preview.addEventListener('click',event=>{if(event.target===preview){const r=preview.getBoundingClientRect();if(event.clientX<r.left||event.clientX>r.right||event.clientY<r.top||event.clientY>r.bottom)preview.close();}});
