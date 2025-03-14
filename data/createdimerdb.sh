#!/bin/sh -e

changeIndex() {
    awk 'FNR==NR{
        chain_len[$1] = $3; next
    } BEGIN{
        i=0
        d=0
    } {
        print d"\t"i"\t"chain_len[$1]
        i += chain_len[$1]
        d++
    }' "${1}.index" "${2}.index" > "${3}.index"
}

if [ -e "${IN}.dbtype" ]; then
    # shellcheck disable=SC2086
    "$MMSEQS" filterdimerdb "${IN}" "${TMP_PATH}/contactlist" ${FILTERDIMERDB_PAR} \
        || fail "filterdimerdb died"
    "$MMSEQS" lndb "${IN}" "${OUT}" \
        || fail "lndb died"
    "$MMSEQS" lndb "${IN}_ss" "${OUT}_ss" \
        || fail "lndb died"
    "$MMSEQS" lndb "${IN}_ca" "${OUT}_ca" \
        || fail "lndb died"
    "$MMSEQS" lndb "${IN}_h" "${OUT}_h" \
        || fail "lndb died"

    rm "${OUT}.index"
    rm "${OUT}_ss.index"
    rm "${OUT}_ca.index"
    rm "${OUT}_h.index"

    changeIndex "${IN}" "${TMP_PATH}/contactlist" "${OUT}"
    changeIndex "${IN}_ss" "${TMP_PATH}/contactlist" "${OUT}_ss"
    changeIndex "${IN}_ca" "${TMP_PATH}/contactlist" "${OUT}_ca"
    changeIndex "${IN}_h" "${TMP_PATH}/contactlist" "${OUT}_h"
    
    if [ -e "${OUT}.lookup" ]; then 
        rm "${OUT}.lookup"
    fi
    if [ -e "${OUT}.source" ]; then 
        rm "${OUT}.source"
    fi
    
    awk 'FNR==NR {
        chainname[$1] = $2; next
    } BEGIN {i = 0} {
        print i"\tDI"int(i/2)"_"chainname[$1]"\t"int(i/2)
        i++
    }' "${IN}.lookup" "${TMP_PATH}/contactlist.index" > "${OUT}.lookup"

    awk 'NR%2==1 {
        sub(/_[^_]+$/, "", $2)
        print $3"\t"$2
    }' "${OUT}.lookup" > "${OUT}.source"
fi

if [ -n "${REMOVE_TMP}" ]; then
    # shellcheck disable=SC2086
    "$MMSEQS" rmdb "${TMP_PATH}/contactlist"
fi
