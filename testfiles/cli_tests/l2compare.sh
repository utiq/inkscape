#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Convert an image (or a single page of a PDF/PostScript document) to a bitmap
# and calculate the relative root-mean-squared (L2) distance from the reference.
#
# Authors:
#   Rafael Siejakowski <rs@rs-math.net>
#
# Copyright (C) 2022 Authors
#
# Released under GNU GPL v2+, read the file 'COPYING' for more information.
#

ensure_command()
{
    command -v $1 >/dev/null 2>&1 || { echo >&2 "Required command '$1' not found. Aborting."; exit 1; }
}

ensure_command "convert"
ensure_command "compare"
ensure_command "bc"
ensure_command "cp"

OUTPUT_FILENAME="$1"
OUTPUT_PAGE="$2"
REFERENCE_FILENAME="$3"
PERCENTAGE_DIFFERENCE_ALLOWED="$4"
DPI="$5"

if [ ! -f "${OUTPUT_FILENAME}" ]
then
    echo "Error: Test file '${OUTPUT_FILENAME}' not found."
    exit 1
fi

if [ ! -f "${REFERENCE_FILENAME}" ]
then
    echo "Error: Reference file '${REFERENCE_FILENAME}' not found."
    exit 1
fi

# Convert the output file to the PNG format
CONVERSION_OPTIONS="-colorspace RGB"

# Extract a page from multipage PS/PDF if requested
OUTFILE_SUFFIX=""
if [[ "x$OUTPUT_PAGE" != "x" ]]
then
    OUTFILE_SUFFIX="[${OUTPUT_PAGE}]" # Use ImageMagick's bracket operator
fi

DPI_OPTION=""
if [[ "x$DPI" != "x" ]]
then
    DPI_OPTION="-density $DPI"
fi

if ! convert $DPI_OPTION "${OUTPUT_FILENAME}${OUTFILE_SUFFIX}" $CONVERSION_OPTIONS "${OUTPUT_FILENAME}-output.png"
then
    echo "Warning: Failed to convert test file '${OUTPUT_FILENAME}' to PNG format. Skipping comparison test."
    exit 42
fi

# Copy the reference file
cp "${REFERENCE_FILENAME}" "${OUTPUT_FILENAME}-reference.png"

# Compare the two files
COMPARE_RESULT=$(compare 2>&1 -metric RMSE "${OUTPUT_FILENAME}-output.png" "${OUTPUT_FILENAME}-reference.png" \
                 "${OUTPUT_FILENAME}-diff.png")
COMPARE_RESULT=${COMPARE_RESULT#*(}
RELATIVE_ERROR=${COMPARE_RESULT%)*}
if [[ "x$RELATIVE_ERROR" == "x" ]]
then
    echo "Warning: Could not parse out the relative RMS error for fuzzy comparison. Skipping comparison test."
    exit 42
fi

# Check if the difference between the files is within tolerance
CONDITION="$RELATIVE_ERROR * 100 <= $PERCENTAGE_DIFFERENCE_ALLOWED"
WITHIN_TOLERANCE=$(echo "${CONDITION}" | bc)
if [[ $? -ne 0 ]]
then
    echo "Warning: An error occurred running 'bc'. The fuzzy comparison test will be skipped."
    exit 42
fi

PERCENTAGE_ERROR=$(echo "$RELATIVE_ERROR * 100" | bc)
if (( $WITHIN_TOLERANCE ))
then
    # Test passed: print stats and clean up the files.
    echo "Fuzzy comparison PASSED; error of ${PERCENTAGE_ERROR}% is within ${PERCENTAGE_DIFFERENCE_ALLOWED}% tolerance."
    for FILE in ${OUTPUT_FILENAME}{,-reference.png,-output.png,-diff.png}
    do
        rm -f "${FILE}"
    done
else
    # Test failed!
    echo "Fuzzy comparison FAILED; error of ${PERCENTAGE_ERROR}% exceeds ${PERCENTAGE_DIFFERENCE_ALLOWED}% tolerance."
    exit 1
fi

