#!/usr/bin/env bash
# Reproduces the evaluation, in three tiers. Run from anywhere; it works in the repository root.
#
#   eval/reproduce.sh check       (default, ~10 minutes, no network) builds the compiler, runs every
#                                 test, checks that the paper's numbers are the ones the committed
#                                 results give, and reruns a small differential test and census
#   eval/reproduce.sh full        (hours, network) downloads the inputs and reruns every experiment
#                                 of the paper with PrepLang, then regenerates the paper's numbers;
#                                 the committed results in eval/results/ are overwritten
#   eval/reproduce.sh baselines   (after full) Great Expectations, pandera and TFDV, and private
#                                 compilation (RQ8) with HDMM and AIM, which need their own Python
#                                 environments (eval/README.md)
#
# Environment: PYTHON (default python3, or py -3 on Windows) must have pandas, numpy, pyarrow and
# opencv-python; CMake 3.16+ and a C++17 compiler. Docker gives all of this: see the Dockerfile.
# Timings depend on the machine, so time and memory numbers differ from the paper's; counts and
# correctness outcomes (every "0 unsound", every "N of M") must not.
set -euo pipefail

cd "$(dirname "$0")/.."
tier=${1:-check}

if [ -n "${PYTHON:-}" ]; then
    read -r -a python <<< "$PYTHON"
elif command -v py > /dev/null 2>&1; then
    python=(py -3)
else
    python=(python3)
fi

step() { printf '\n==== %s\n' "$*"; }
run() { echo "+ ${python[*]} $*"; "${python[@]}" "$@"; }

build() {
    step "Build the compiler (Release) and run the tests"
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPython3_EXECUTABLE="$("${python[@]}" -c 'import sys; print(sys.executable)')"
    cmake --build build --config Release --parallel
    ctest --test-dir build -C Release --output-on-failure
    find_compiler
}

find_compiler() {
    if [ -f build/Release/preplang.exe ]; then compiler=build/Release/preplang.exe; else compiler=build/preplang; fi
    "$compiler" --version
}

check_numbers() {
    step "The paper's numbers are the committed results' numbers"
    cp paper/generated/numbers.tex "${TMPDIR:-/tmp}/numbers.before.tex"
    run eval/paper_numbers.py
    if diff -q --strip-trailing-cr "${TMPDIR:-/tmp}/numbers.before.tex" paper/generated/numbers.tex > /dev/null; then
        echo "paper/generated/numbers.tex: unchanged"
    else
        echo "paper/generated/numbers.tex differs from the committed results:" >&2
        diff --strip-trailing-cr "${TMPDIR:-/tmp}/numbers.before.tex" paper/generated/numbers.tex >&2 || true
        exit 1
    fi
}

case "$tier" in
check)
    build
    check_numbers
    step "A small differential test (200 programs) and census, into eval/work/quick/"
    run eval/evaluate.py --quick
    ;;

full)
    build
    step "Inputs: generated data, public datasets and the frozen script corpora"
    run eval/compilation/datasets.py examples
    run eval/compilation/datasets.py benchmark
    run eval/compilation/datasets.py scale
    run eval/reuse/realbatches.py download
    run eval/corpora.py fetch || echo "some corpus files are no longer on GitHub (listed above); the rest are used"
    run eval/corpora.py verify || echo "the Kaggle Titanic and House Prices files are downloaded by hand: eval/README.md"

    step "Compilation: injected mistakes, optimizations, scale, real bugs, differential test (RQ1, RQ2)"
    run eval/compilation/run_eval.py "$compiler"
    run eval/compilation/bench.py "$compiler"
    run eval/compilation/scale.py "$compiler"
    run eval/compilation/run_realbugs.py "$compiler"
    run eval/compilation/differential.py --programs 2000

    step "Reuse on new batches (RQ3 to RQ8)"
    run eval/reuse/certificates.py "$compiler"
    run eval/reuse/imagebatches.py "$compiler"
    run eval/reuse/realbatches.py "$compiler"
    if [ -f eval/data/titanic/train.csv ]; then run eval/reuse/pandas_corpus.py titanic "$compiler"; fi
    if [ -f eval/data/houses/kaggle_train.csv ]; then run eval/reuse/pandas_corpus.py houses "$compiler"; fi
    for corpus in traffic bikes hours; do run eval/reuse/drift_corpus.py "$corpus" "$compiler"; done
    run eval/reuse/composition.py "$compiler"
    run eval/reuse/overhead.py "$compiler"
    run eval/reuse/certcost.py "$compiler"
    run eval/reuse/certsize.py "$compiler"

    step "Census of the examples and lifted programs (needs the reuse runs above)"
    run eval/compilation/census.py

    step "The paper's numbers and the six-question evaluation"
    run eval/evaluate.py
    echo "Compare with the committed results: git diff --stat eval/results paper/generated"
    ;;

baselines)
    find_compiler
    step "Validators: pandera and Great Expectations in eval/.venv-baselines, TFDV in WSL (Windows only;"
    echo "elsewhere build eval/reuse/tfdv.Dockerfile and run eval/reuse/baselines.py --tools tfdv in it)"
    run eval/reuse/certificates.py validators
    run eval/reuse/realbatches.py validators
    step "Private compilation (RQ8) with its baselines: HDMM in eval/.venv-hdmm, AIM in eval/.venv-dp"
    run eval/reuse/private.py "$compiler"
    run eval/reuse/private.py diagnostics "$compiler"
    run eval/evaluate.py
    ;;

*)
    echo "usage: eval/reproduce.sh [check|full|baselines]" >&2
    exit 2
    ;;
esac
