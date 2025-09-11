#!/usr/bin/env python3

import glob
import pathlib
import shutil
import subprocess
import sys
import tempfile
from typing import List


def prechecks():
    """Check if dnscvt binary exists and is executable."""
    script_dir = pathlib.Path(__file__).parent
    dnscvt_path = script_dir / 'dnscvt'
    
    if not dnscvt_path.exists():
        print("ERROR: dnscvt binary not found")
        sys.exit(1)
    if not dnscvt_path.is_file():
        print("ERROR: dnscvt is not a regular file")
        sys.exit(1)


def run_test(input_path: pathlib.Path, gold_path: pathlib.Path, args: List[str] = None):
    """Test dnscvt by comparing output with gold standard."""
    if args is None:
        args = []
    script_dir = pathlib.Path(__file__).parent
    dnscvt_path = script_dir / 'dnscvt'
    
    # Check if required files exist
    if not input_path.exists():
        print(f"ERROR: {input_path} not found")
        return False
    if not gold_path.exists():
        print(f"ERROR: {gold_path} not found")
        return False
    
    with tempfile.TemporaryDirectory() as temp_dir:
        temp_dir_path = pathlib.Path(temp_dir)
        
        temp_input = temp_dir_path / input_path.name
        shutil.copy2(input_path, temp_input)
        
        cmd = [str(dnscvt_path), *args, str(temp_input)]
        
        # Run dnscvt on input file
        try:
            result = subprocess.run(cmd, 
                                  cwd=temp_dir, 
                                  capture_output=True, 
                                  text=True)
            if result.returncode != 0:
                print(f"ERROR: dnscvt failed with return code {result.returncode}")
                print(f"stderr: {result.stderr}")
                return False
        except Exception as e:
            print(f"ERROR: Failed to run dnscvt: {e}")
            return False
        
        # Check if output file was created (replace .txt with .raw)
        output_name = temp_input.stem + '.raw'
        temp_output = temp_dir_path / output_name
        if not temp_output.exists():
            print(f"ERROR: {output_name} was not created")
            return False
        
        try:
            with open(temp_output, 'rb') as f1, open(gold_path, 'rb') as f2:
                output_content = f1.read()
                gold_content = f2.read()
                
                if output_content == gold_content:
                    print(f"SUCCESS: {input_path.name} -> {output_name} matches {gold_path.name}")
                    return True
                else:
                    print(f"FAILURE: {input_path.name} -> {output_name} does not match {gold_path.name}")
                    print(f"Generated file size: {len(output_content)} bytes")
                    print(f"Gold file size: {len(gold_content)} bytes")
                    return False
        except Exception as e:
            print(f"ERROR: Failed to compare files: {e}")
            return False


def main():
    prechecks()
    
    all_passed = True
    for input_file in glob.glob("tests/*.txt"):
        input_path = pathlib.Path(input_file)
        gold_path = input_path.with_suffix('.gold')
        
        all_passed = all_passed and run_test(input_path, gold_path)
    
    sys.exit(0 if all_passed else 1)


if __name__ == "__main__":
    main()
