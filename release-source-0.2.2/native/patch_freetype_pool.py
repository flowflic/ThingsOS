"""Generate a per-raster gray workspace patch; pinned upstream stays intact.

FreeType license/copyright headers are retained in every generated source.
ESP builds allocate strictly from PSRAM; host uses the FT allocator for tests.
"""
from pathlib import Path
import argparse,hashlib,json
def generate(source,out):
    source=Path(source);out=Path(out);out.mkdir(parents=True,exist_ok=True)
    original=(source/'ftgrays.c').read_text(encoding='utf-8')
    s=original
    def replace(a,b):
        nonlocal s
        assert s.count(a)==1,a
        s=s.replace(a,b)
    replace('#define FT_COMPONENT  smooth','#define FT_COMPONENT  smooth\n\n/* ThingsOS local patch: a reusable pool belongs to each raster instance. */\n#if defined( ESP_PLATFORM ) && !defined( STANDALONE_ )\n#include "esp_heap_caps.h"\n#include "esp_log.h"\n#endif')
    replace('    PCell*      ycells;','    PCell       pool;\n\n    PCell*      ycells;')
    replace('    void*  memory;','    void*  memory;\n    PCell  pool;')
    replace('    TCell    buffer[FT_MAX_GRAY_POOL];','''#ifdef STANDALONE_
    TCell    buffer[FT_MAX_GRAY_POOL];
#else
    PCell    buffer = ras.pool;
#endif''')
    replace('    return gray_convert_glyph( RAS_VAR );','''#ifndef STANDALONE_
    {
      gray_PRaster owner = (gray_PRaster)raster;
      if ( !owner->pool )
      {
#ifdef ESP_PLATFORM
        owner->pool = (PCell)heap_caps_malloc(
          FT_MAX_GRAY_POOL * sizeof ( TCell ),
          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT );
        if ( !owner->pool )
          return FT_THROW( Out_Of_Memory );
        ESP_LOGI( "ThingsOS_font", "Gray workspace PSRAM: %u bytes",
                  (unsigned)( FT_MAX_GRAY_POOL * sizeof ( TCell ) ) );
#else
        FT_Memory memory = (FT_Memory)owner->memory;
        FT_Error error;
        if ( FT_QALLOC( owner->pool,
                        FT_MAX_GRAY_POOL * sizeof ( TCell ) ) )
          return error;
#endif
      }
      ras.pool = owner->pool;
    }
#endif
    return gray_convert_glyph( RAS_VAR );''')
    replace('    FT_FREE( raster );','''#ifdef ESP_PLATFORM
    heap_caps_free( ((gray_PRaster)raster)->pool );
#else
    FT_FREE( ((gray_PRaster)raster)->pool );
#endif
    FT_FREE( raster );''')
    records={}
    for p in sorted(source.iterdir()):
        if not p.is_file():continue
        data=s.encode('utf-8') if p.name=='ftgrays.c' else p.read_bytes()
        target=out/p.name
        if not target.exists() or target.read_bytes()!=data:target.write_bytes(data)
        records[p.name]=hashlib.sha256(data).hexdigest()
    meta={'patch':'ThingsOS per-raster PSRAM gray workspace v1',
          'upstream_sha256':hashlib.sha256(original.encode('utf-8')).hexdigest(),
          'generated':records,'pool_bytes':16384,'internal_fallback':False}
    (out/'patch.json').write_text(json.dumps(meta,indent=2)+'\n',encoding='utf-8')
    return meta
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();generate(a.source,a.output)
