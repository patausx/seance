#!/usr/bin/env python3
"""Private REC native Gossip28 labels and Gossip56 timer; shared font unchanged."""
import argparse
from pathlib import Path
import generate_filter_type as donor
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'platform/3ds/rec_type_data.h'
def generate():
    # Keep the established packer, changing only private words and numeral size.
    source=(ROOT/'tools/generate_filter_type.py').read_text()
    source=source[:source.index("if __name__")].replace('bake(char,40)','bake(char,56)').replace('height<48','height<64')
    namespace={'__file__':str(ROOT/'tools/generate_filter_type.py')}
    exec(source,namespace)
    namespace['WORDS']={28:('rec','setup','stop','record','pause','resume','new take','gran','play')}
    namespace['CHARS']='0123456789.:'
    return namespace['generate']().replace('generate_filter_type.py','generate_rec_type.py').replace('Seance Filter','Seance Rec').replace('::filter_type','::rec_type')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--check',action='store_true');args=p.parse_args();text=generate()
    if args.check:assert OUT.read_text()==text
    else:
        assert not OUT.exists(),'back up generated masks before replacing'
        OUT.write_text(text)
    print('rec type: native masks '+('verified' if args.check else 'generated'))
