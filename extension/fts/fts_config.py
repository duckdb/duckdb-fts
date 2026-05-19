import glob
import os

# list all include directories
include_directories = [
    os.path.sep.join(x.split('/'))
    for x in [
        'extension/fts/include',
        'third_party/snowball/include',
        'third_party/snowball/libstemmer',
        'third_party/snowball/runtime',
        'third_party/snowball/src_c',
    ]
]
# source files
source_files = [
    os.path.sep.join(x.split('/')) for x in ['extension/fts/fts_extension.cpp', 'extension/fts/fts_indexing.cpp']
]
# snowball
source_files += [
    os.path.sep.join(x.split('/'))
    for x in [
        'third_party/snowball/libstemmer/libstemmer.c',
        'third_party/snowball/runtime/api.c',
        'third_party/snowball/runtime/utilities.c',
    ]
]
source_files += sorted(
    glob.glob(os.path.sep.join(['third_party', 'snowball', 'src_c', 'stem_*.c']))
)
