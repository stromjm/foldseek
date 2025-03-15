#!/bin/sh -e

changeIndex() {
    awk 'FNR==NR{
        chain_off[$1] = $2;
        chain_len[$1] = $3; next
    } BEGIN{
        d=0
    } {
        print d"\t"chain_off[$1]"\t"chain_len[$1]
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
    # shellcheck disable=SC2086
    "$MMSEQS" rmdb "${OUT}_h" \
        || fail "rmdb died"

    changeIndex "${IN}" "${TMP_PATH}/contactlist" "${OUT}"
    changeIndex "${IN}_ss" "${TMP_PATH}/contactlist" "${OUT}_ss"
    changeIndex "${IN}_ca" "${TMP_PATH}/contactlist" "${OUT}_ca"
    
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

    # shellcheck disable=SC2086
    "$MMSEQS" tsv2db "${OUT}.lookup" "${OUT}_h" ${VERBOSITY_PAR} \
        || fail "tsv2db died"

fi

if [ -n "${REMOVE_TMP}" ]; then
    # shellcheck disable=SC2086
    "$MMSEQS" rmdb "${TMP_PATH}/contactlist"
fi
