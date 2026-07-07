
import sys
import os
import shutil
import re

def is_timestamp_line(line):
    """Checks if a line is a VTT/SRT timestamp line."""
    return '-->' in line

def has_sequence_number(lines, index):
    """Checks if the line before the timestamp is a number."""
    if index == 0:
        return False
    prev_line_index = -1
    for i in range(index - 1, -1, -1):
        if lines[i].strip():
            prev_line_index = i
            break
    if prev_line_index != -1:
        return lines[prev_line_index].strip().isdigit()
    return False

def convert_vtt_to_srt(file_path):
    """
    Converts a VTT or VTT-like SRT file to a standard SRT file.
    - Creates a backup (.bak).
    - Removes VTT headers.
    - Converts timestamp decimal separator from '.' to ','.
    - Adds sequence numbers if they are missing.
    """
    if not os.path.exists(file_path):
        print(f"Error: File not found at {file_path}")
        return

    # 1. Create a backup
    backup_path = file_path + '.bak'
    try:
        shutil.copy(file_path, backup_path)
        print(f"Backup created at {backup_path}")
    except Exception as e:
        print(f"Error creating backup for {file_path}: {e}")
        return

    # 2. Read file content
    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            lines = f.readlines()
    except Exception as e:
        print(f"Error reading {file_path}: {e}")
        return

    output_lines = []
    srt_sequence_counter = 1
    in_header = True
    
    # Check if the file already has sequence numbers
    file_has_sequence_numbers = False
    for i, line in enumerate(lines):
        if is_timestamp_line(line):
            if has_sequence_number(lines, i):
                file_has_sequence_numbers = True
                break

    for i, line in enumerate(lines):
        line_strip = line.strip()
        
        # Skip VTT header lines
        if in_header:
            if line_strip.upper().startswith('WEBVTT') or \
               line_strip.upper().startswith('KIND:') or \
               line_strip.upper().startswith('STYLE:') or \
               line_strip.upper().startswith('NOTE') or \
               not line_strip:
                continue
        
        in_header = False

        if is_timestamp_line(line):
            # Add sequence number if the file doesn't have them
            if not file_has_sequence_numbers:
                output_lines.append(str(srt_sequence_counter) + '\n')
                srt_sequence_counter += 1
            
            # Convert timestamp format
            output_lines.append(line.replace('.', ',', 2))
        else:
            # If the file has sequence numbers, they will be preserved here.
            output_lines.append(line)

    # 3. Write the converted content back to the file
    try:
        with open(file_path, 'w', encoding='utf-8') as f:
            f.writelines(output_lines)
        print(f"Successfully converted {file_path} to standard SRT format.")
    except Exception as e:
        print(f"Error writing converted content to {file_path}: {e}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python convert_vtt_to_srt.py <file1.srt> <file2.srt> ...")
        sys.exit(1)
    
    for file_path in sys.argv[1:]:
        convert_vtt_to_srt(file_path)
