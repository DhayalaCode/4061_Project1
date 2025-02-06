#!/bin/bash
# test_extract_complex.sh
# This script creates several test files in the current directory,
# archives them using minitar, deletes the originals, extracts the archive,
# and verifies the extracted contents.

# Exit immediately if any command fails.
set -e

ARCHIVE="test_archive.tar"

# Clean up any previous test files and archive.
rm -f "$ARCHIVE" test1.txt test2.txt test3.txt test4.txt test5.txt

echo "Creating test files..."

# Test file 1: A simple text file.
echo "Hello, this is test file 1." > test1.txt

# Test file 2: A file with 600 characters (all 'B').
head -c 600 < /dev/zero | tr '\0' 'B' > test2.txt

# Test file 3: A file containing special characters.
echo "Test file 3 with special characters: !@#$%^&*()" > test3.txt

# Test file 4: An empty file.
> test4.txt

# Test file 5: A file with multiple lines.
cat <<EOF > test5.txt
Lorem ipsum dolor sit amet, consectetur adipiscing elit.
Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.
Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat.
EOF

echo "Creating archive with minitar..."
# Archive all test files into one tar file.
./minitar -c -f "$ARCHIVE" test1.txt test2.txt test3.txt test4.txt test5.txt
if [ $? -ne 0 ]; then
    echo "Archive creation failed."
    exit 1
fi

echo "Archive creation complete."

# Remove the original files so extraction is evident.
rm -f test1.txt test2.txt test3.txt test4.txt test5.txt

echo "Extracting files from archive..."
./minitar -x -f "$ARCHIVE"
if [ $? -ne 0 ]; then
    echo "Extraction failed."
    exit 1
fi
echo "Extraction complete."

echo "Verifying extracted files..."

# Verify test1.txt
EXPECTED1="Hello, this is test file 1."
EXTRACTED1=$(cat test1.txt)
if [ "$EXPECTED1" != "$EXTRACTED1" ]; then
    echo "test1.txt content mismatch."
    exit 1
fi

# Verify test2.txt: Expect 600 'B's.
EXPECTED2=$(printf 'B%.0s' {1..600})
EXTRACTED2=$(head -c 600 test2.txt)
if [ "$EXPECTED2" != "$EXTRACTED2" ]; then
    echo "test2.txt content mismatch."
    exit 1
fi

# Verify test3.txt
EXPECTED3="Test file 3 with special characters: !@#$%^&*()"
EXTRACTED3=$(cat test3.txt)
if [ "$EXPECTED3" != "$EXTRACTED3" ]; then
    echo "test3.txt content mismatch."
    exit 1
fi

# Verify test4.txt: Should be empty.
if [ -s test4.txt ]; then
    echo "test4.txt is not empty."
    exit 1
fi

# Verify test5.txt: Multi-line content.
read -r -d '' EXPECTED5 <<'EOF'
Lorem ipsum dolor sit amet, consectetur adipiscing elit.
Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.
Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat.
EOF
EXTRACTED5=$(cat test5.txt)
if [ "$EXPECTED5" != "$EXTRACTED5" ]; then
    echo "test5.txt content mismatch."
    exit 1
fi

echo "All tests passed successfully."

# Cleanup: Remove the archive and extracted test files.
rm -f "$ARCHIVE" test1.txt test2.txt test3.txt test4.txt test5.txt

exit 0

