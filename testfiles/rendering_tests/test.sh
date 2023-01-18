#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later

ensure_command()
{
    command -v $1 >/dev/null 2>&1 || { echo >&2 "Required command '$1' not found. Aborting."; exit 1; }
}

export LANG=C # Needed to force . as the decimal separator

ensure_command "compare"
ensure_command "bc"

if [ "$#" -lt 2 ]; then
    echo "Pass the path of the inkscape executable as parameter then the name of the test" $#
    exit 1
fi

INKSCAPE_EXE="$1"
TEST="$2"
FUZZ="$3"
EXIT_STATUS=0
EXPECTED="$(dirname "$TEST")/expected_rendering/$(basename "$TEST")"
TESTNAME="$(basename "$TEST")"

if [ "$FUZZ" = "" ]; then
    METRIC="AE"
else
    METRIC="RMSE"
fi

perform_test()
{
    local SUFFIX="$1"
    local DPI="$2"
    ${INKSCAPE_EXE} --export-png-use-dithering false --export-filename="${TESTNAME}${SUFFIX}.png" -d "$DPI" "${TEST}.svg"

    COMPARE_RESULT="$(compare -metric "$METRIC" "${TESTNAME}${SUFFIX}.png" "${EXPECTED}${SUFFIX}.png" "${TESTNAME}-compare${SUFFIX}.png" 2>&1)"

    if [ "$FUZZ" = "" ]; then
        if [ "$COMPARE_RESULT" = 0 ]; then
            echo "${TESTNAME}${SUFFIX}" "PASSED; absolute difference is exactly zero."
            rm "${TESTNAME}${SUFFIX}.png" "${TESTNAME}-compare${SUFFIX}.png"
        else
            echo "${TESTNAME} FAILED; absolute difference ${COMPARE_RESULT} is greater than zero."
            EXIT_STATUS=1
        fi
    else
        RELATIVE_ERROR=${COMPARE_RESULT#*(}
        RELATIVE_ERROR=${RELATIVE_ERROR%)*}
        if [ "$RELATIVE_ERROR" = "" ]; then
            echo "${TESTNAME} FAILED; could not parse relative error '${COMPARE_RESULT}'."
            EXIT_STATUS=1
            return
        fi

        CONDITION=$(printf "%.12f * 100 <= $FUZZ" "$RELATIVE_ERROR")
        WITHIN_TOLERANCE=$(echo "${CONDITION}" | bc)
        if [[ $? -ne 0 ]]; then
            echo "${TESTNAME} FAILED; An error occurred running 'bc'."
            EXIT_STATUS=1
            return
        fi

        PERCENTAGE_ERROR_FORMULA=$(printf "%.4f * 100" "$RELATIVE_ERROR")
        PERCENTAGE_ERROR=$(echo "${PERCENTAGE_ERROR_FORMULA}" | bc)
        if (( $WITHIN_TOLERANCE )); then
            echo "${TESTNAME}${SUFFIX}" "PASSED; error of ${PERCENTAGE_ERROR}% is within ${FUZZ}% tolerance."
            rm "${TESTNAME}${SUFFIX}.png" "${TESTNAME}-compare${SUFFIX}.png"
        else
            echo "${TESTNAME} FAILED; error of ${PERCENTAGE_ERROR}% exceeds ${FUZZ}% tolerance."
            EXIT_STATUS=1
        fi
    fi
}

perform_test "" 96

if [ -f "${EXPECTED}-large.png" ]; then
    perform_test "-large" 384
else
    echo "${TESTNAME}-large" "SKIPPED"
fi

exit $EXIT_STATUS
