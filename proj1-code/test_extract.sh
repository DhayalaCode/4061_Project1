#!/bin/bash

# Create a test file with known content.
echo "Hello, world!" > test.txt

# Create the archive using your minitar program.
./minitar -c -f test.tar test.txt
if [ $? -ne 0 ]; then
    echo "Archive creation failed."
    exit 1
fi

# Remove the original file.
rm test.txt

# Extract files from the archive.
./minitar -x -f test.tar
if [ $? -ne 0 ]; then
    echo "Extraction failed."
    exit 1
fi

# Check if the file exists and matches expected content.
if [ ! -f test.txt ]; then
    echo "Extracted file not found."
    exit 1
fi

# Compare the extracted file with the expected content.
echo "Hello, world!" > expected.txt
if diff test.txt expected.txt; then
    echo "Test passed: Extracted file matches expected content."
else
    echo "Test failed: File content does not match."
fi

# Cleanup temporary files.
rm test.txt test.tar expected.txt
