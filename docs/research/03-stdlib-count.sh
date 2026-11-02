#!/bin/sh
# Read source as text only. The required argument is the repository source root.
# Counts physical records (including comments/blanks), not executable SLOC.
set -eu
LC_ALL=C
export LC_ALL
if [ "$#" -ne 1 ]; then
    printf 'Usage: %s SOURCE_ROOT\n' "$0" >&2
    exit 2
fi
root=$1
cd "$root"

# Each input record is a filename. Source contents are never evaluated.
count_paths() {
    awk -v label="$1" '
        { files++; while ((getline line < $0) > 0) lines++; close($0) }
        END { printf "%s\t%d\t%d\n", label, files, lines }
    '
}

printf 'CPython physical lines: label\tfiles\tlines\n'
for dir in Objects Python Modules Parser; do
    find "vendor/cpython/$dir" -type f -name '*.c' | sort | count_paths "$dir/*.c (recursive)"
done
find vendor/cpython/Lib -type f -name '*.py' ! -path 'vendor/cpython/Lib/test/*' |
    sort | count_paths 'Lib/*.py excluding Lib/test'
find vendor/cpython/Lib/test -type f -name '*.py' |
    sort | count_paths 'Lib/test/*.py'
for dir in Objects Python Modules Parser; do
    find "vendor/cpython/$dir" -type f \( -name '*.h' -o -name '*.inc' \) |
        sort | count_paths "$dir headers/includes (supplement)"
done

printf '\nModules physical .c accounting: path bucket\tfiles\tlines\n'
find vendor/cpython/Modules -type f -name '*.c' | sort | awk '
    {
        path=$0; key=path; sub(/^vendor\/cpython\/Modules\//, "", key)
        if (index(key, "/")) sub(/\/.*/, "/", key)
        n=0; while ((getline line < path) > 0) n++; close(path)
        files[key]++; lines[key]+=n; totalfiles++; total+=n
    }
    END {
        for (key in files) printf "%s\t%d\t%d\n",key,files[key],lines[key]
        printf "TOTAL\t%d\t%d\n",totalfiles,total
    }
' | sort

# These recipes name all configured POSIX module translation units, including
# test modules. Transitive #includes / linked libraries are deliberately not
# attributed to a recipe: the path accounting above counts those once.
printf '\nSetup module recipes: module\tdirect .c files\tlines\n'
awk '
    {
        sub(/#.*/, ""); sub(/^@MODULE_[A-Z0-9_]+_TRUE@/, "")
        if ($1 !~ /^[A-Za-z_][A-Za-z0-9_]*$/) next
        module=$1; files=0; lines=0
        for (i=2;i<=NF;i++) if ($i ~ /\.c$/) {
            path="vendor/cpython/Modules/" $i; files++
            while ((getline line < path) > 0) lines++
            close(path)
        }
        if (files) printf "%s\t%d\t%d\n",module,files,lines
    }
' vendor/cpython/Modules/Setup.bootstrap.in vendor/cpython/Modules/Setup.stdlib.in | sort

printf '\nPyPy source partitions: label\tfiles\tlines\n'
find vendor/pypy3/pypy/objspace/std -maxdepth 1 -type f -name '*.py' |
    sort | count_paths 'objspace/std top-level implementation-side Python'
find vendor/pypy3/pypy/module -type f -name '*.py' |
    awk '$0 !~ /\/(test|tests|testing|bench|benchmarks)\// && $0 !~ /\/(test_|apptest_|conftest\.py)/' |
    sort | count_paths 'pypy/module non-test Python (all sides)'
find vendor/pypy3/pypy/module -type f -name 'app_*.py' |
    awk '$0 !~ /\/(test|tests|testing|bench|benchmarks)\//' |
    sort | count_paths 'pypy/module app_*.py subset'
find vendor/pypy3/lib_pypy -type f -name '*.py' |
    awk '$0 !~ /\/(test|tests|testing|bench|benchmarks)\// && $0 !~ /\/(test_|apptest_|conftest\.py)/' |
    sort | count_paths 'lib_pypy non-test Python including bundled dependencies'
find vendor/pypy3/lib_pypy/cffi -type f -name '*.py' |
    awk '$0 !~ /\/(test|tests|testing|bench|benchmarks)\// && $0 !~ /\/(test_|apptest_|conftest\.py)/' |
    sort | count_paths 'lib_pypy/cffi subset'
find vendor/pypy3/lib_pypy -type f -name '*_build.py' |
    sort | count_paths 'lib_pypy *_build.py subset'
find vendor/pypy3/pypy/module -type f -name moduledef.py | sort | awk '
    { n++; path=$0; sub(/^vendor\/pypy3\/pypy\/module\//,"",path); sub(/\/moduledef.py$/,"",path); print path }
    END { printf "MODULEDEF_COUNT\t%d\n",n }
'
printf '\nPyPy per module: module\timplementation-side files/lines\tapp_* files/lines\n'
find vendor/pypy3/pypy/module -type f -name '*.py' |
    awk '$0 !~ /\/(test|tests|testing|bench|benchmarks)\// && $0 !~ /\/(test_|apptest_|conftest\.py)/' |
    sort | awk '
        {
            path=$0; key=path; sub(/^vendor\/pypy3\/pypy\/module\//,"",key); sub(/\/.*/,"",key)
            app=(path ~ /\/app_[^/]*\.py$/); n=0
            while ((getline line < path)>0) n++; close(path)
            if (app) { af[key]++; al[key]+=n } else { f[key]++; l[key]+=n }
            keys[key]=1
        }
        END { for (key in keys) printf "%s\t%d/%d\t%d/%d\n",key,f[key],l[key],af[key],al[key] }
    ' | sort

printf '\nCPython candidate reuse cohort (not all standalone fallbacks): file\tlines\n'
for path in _pydecimal.py _pydatetime.py _pyio.py pickle.py bisect.py heapq.py functools.py \
    collections/__init__.py json/__init__.py json/decoder.py json/encoder.py json/scanner.py \
    _strptime.py operator.py _py_abc.py statistics.py stat.py queue.py \
    xml/etree/ElementTree.py _py_warnings.py tokenize.py _pylong.py zoneinfo/_zoneinfo.py; do
    awk -v path="$path" 'END { printf "%s\t%d\n",path,NR }' "vendor/cpython/Lib/$path"
done

printf '\nCPython startup source envelope: file\tlines\n'
for path in importlib/_bootstrap.py importlib/_bootstrap_external.py zipimport.py \
    codecs.py encodings/__init__.py encodings/aliases.py encodings/utf_8.py encodings/ascii.py \
    io.py abc.py _collections_abc.py os.py stat.py genericpath.py posixpath.py ntpath.py \
    site.py _sitebuiltins.py; do
    awk -v path="$path" 'END { printf "%s\t%d\n",path,NR }' "vendor/cpython/Lib/$path"
done
