#!/bin/bash
set -e

SOURCE_DIR="$(pwd)"
BACKUP_NAME="backup-2026-09-07"
DEST_DIR="../${BACKUP_NAME}"

echo "Source:      ${SOURCE_DIR}"
echo "Destination: $(realpath -m "${DEST_DIR}")"
echo ""

if [ -e "${DEST_DIR}" ]; then
    echo "ERROR: ${DEST_DIR} already exists. Refusing to overwrite or merge."
    exit 1
fi

mkdir -p "${DEST_DIR}"

echo "Copying everything, including hidden files..."
cp -a "${SOURCE_DIR}/." "${DEST_DIR}/"

echo ""
echo "Copy complete. Verifying the backup is genuinely identical..."
echo ""

if diff -rq "${SOURCE_DIR}" "${DEST_DIR}" --exclude="${BACKUP_NAME}"; then
    echo ""
    echo "VERIFIED: backup is byte-for-byte identical to the source."
else
    echo ""
    echo "WARNING: differences were reported above."
    exit 1
fi

echo ""
echo "Backup complete and verified at: $(realpath "${DEST_DIR}")"
echo "File count comparison:"
echo "  Source:      $(find "${SOURCE_DIR}" | wc -l) entries"
echo "  Destination: $(find "${DEST_DIR}" | wc -l) entries"
