#!/bin/bash
#
# Shortcut for downloading HF models
#
# Usage:
#   ./main -m $(./examples/hf.sh https://huggingface.co/TheBloke/Mixtral-8x7B-v0.1-GGUF/resolve/main/mixtral-8x7b-v0.1.Q4_K_M.gguf)
#   ./main -m $(./examples/hf.sh --url https://huggingface.co/TheBloke/Mixtral-8x7B-v0.1-GGUF/blob/main/mixtral-8x7b-v0.1.Q4_K_M.gguf)
#

# all logs go to stderr
function log {
    echo "$@" 1>&2
}

function usage {
    log "Usage: $0 --url <url> [--help]"
    exit 1
}

# check for curl or wget
function has_cmd {
    if ! [ -x "$(command -v $1)" ]; then
        return 1
    fi
}

if has_cmd wget; then
    cmd="wget -q --show-progress -c -O %s %s"
elif has_cmd curl; then
    cmd="curl -C - -f -o %s -L %s"
else
    print "Error: curl or wget not found"
    exit 1
fi

url=""

# parse args
while [[ $# -gt 0 ]]; do
    case "$1" in
        --url)
            url="$2"
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        *)
            url="$1"
            shift
            ;;
    esac
done

if [ -z "$url" ]; then
    log "Error: missing --url"
    usage
fi

# check if the URL is a HuggingFace model, and if so, try to download it
is_url=false

if [[ ${#url} -gt 22 ]]; then
    if [[ ${url:0:22} == "https://huggingface.co" ]]; then
        is_url=true
    fi
fi

if [ "$is_url" = false ]; then
    exit 0
fi

# replace "blob/main" with "resolve/main"
url=${url/blob\/main/resolve\/main}

basename=$(basename $url)

log "[+] attempting to download $basename"

if [ -n "$cmd" ]; then
    cmd=$(printf "$cmd" "$basename" "$url")
    log "[+] $cmd"
    if $cmd; then
        echo $basename
        exit 0
    fi
fi

log "[-] failed to download"

exit 1
