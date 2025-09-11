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
        temp_output = temp_dir_path / "output.raw"
        
        # Use -o flag to specify exact output location
        cmd = [str(dnscvt_path), *args, "-o", str(temp_output), str(input_path)]
        
        # Run dnscvt on input file
        try:
            result = subprocess.run(cmd, 
                                  capture_output=True, 
                                  text=True)
            if result.returncode != 0:
                print(f"ERROR: dnscvt failed with return code {result.returncode}")
                print(f"stderr: {result.stderr}")
                return False
        except Exception as e:
            print(f"ERROR: Failed to run dnscvt: {e}")
            return False
        
        # Check if output file was created
        if not temp_output.exists():
            print(f"ERROR: {temp_output} was not created")
            return False
        
        try:
            with open(temp_output, 'rb') as f1, open(gold_path, 'rb') as f2:
                output_content = f1.read()
                gold_content = f2.read()
                
                if output_content == gold_content:
                    print(f"SUCCESS: {input_path.name} -> output.raw matches {gold_path.name}")
                    return True
                else:
                    print(f"FAILURE: {input_path.name} -> output.raw does not match {gold_path.name}")
                    print(f"Generated file size: {len(output_content)} bytes")
                    print(f"Gold file size: {len(gold_content)} bytes")
                    return False
        except Exception as e:
            print(f"ERROR: Failed to compare files: {e}")
            return False


def test_flag_combinations(input_path: pathlib.Path):
    """Test dnscvt flag combinations and verify expected behavior."""
    script_dir = pathlib.Path(__file__).parent
    dnscvt_path = script_dir / 'dnscvt'
    
    if not input_path.exists():
        print(f"ERROR: {input_path} not found")
        return False
    
    with tempfile.TemporaryDirectory() as temp_dir:
        temp_dir_path = pathlib.Path(temp_dir)
        
        def run_dnscvt(output_file, *args):
            cmd = [str(dnscvt_path), *args, "-o", str(output_file), str(input_path)]
            
            try:
                result = subprocess.run(cmd, capture_output=True, text=True)
                if result.returncode != 0:
                    print(f"ERROR: dnscvt failed with return code {result.returncode}")
                    print(f"stderr: {result.stderr}")
                    return None
                    
                if not output_file.exists():
                    print(f"ERROR: {output_file} was not created")
                    return None
                    
                with open(output_file, 'rb') as f:
                    return f.read()
                    
            except Exception as e:
                print(f"ERROR: Failed to run dnscvt: {e}")
                return None
        
        # Test all flag combinations
        outputs = {}
        outputs["no_flags"] = run_dnscvt(temp_dir_path / "no_flags.raw")
        outputs["edns"] = run_dnscvt(temp_dir_path / "edns.raw", "-e")
        outputs["dnssec"] = run_dnscvt(temp_dir_path / "dnssec.raw", "-D")
        outputs["edns_dnssec"] = run_dnscvt(temp_dir_path / "edns_dnssec.raw", "-e", "-D")
        
        # Check if any runs failed
        if None in outputs.values():
            return False
        
        # Verify size relationships
        base_size = len(outputs["no_flags"])
        edns_size = len(outputs["edns"])
        dnssec_size = len(outputs["dnssec"])
        edns_dnssec_size = len(outputs["edns_dnssec"])
        
        # EDNS adds ~11 bytes per query (5 queries = ~55 bytes)
        expected_edns_increase = edns_size - base_size
        if expected_edns_increase <= 0:
            print(f"ERROR: EDNS should increase file size (base: {base_size}, edns: {edns_size})")
            return False
        print(f"INFO: EDNS adds {expected_edns_increase} bytes")
        
        # DNSSEC should have same size as EDNS
        if dnssec_size != edns_size:
            print(f"ERROR: DNSSEC size ({dnssec_size}) should equal EDNS size ({edns_size})")
            return False
        print(f"INFO: DNSSEC size matches EDNS size ({dnssec_size} bytes)")
        
        # -e -D should be same as -D (D wins)
        if outputs["edns_dnssec"] != outputs["dnssec"]:
            print("ERROR: -e -D should produce same output as -D")
            return False
        print("INFO: -e -D produces same output as -D (precedence working)")
        
        # Check that EDNS and DNSSEC differ only in flags field
        # The flags are at offset varies, but we can check they're mostly identical
        if len(outputs["edns"]) != len(outputs["dnssec"]):
            print("ERROR: EDNS and DNSSEC outputs should have same length")
            return False
            
        # Count differing bytes
        diff_count = sum(1 for a, b in zip(outputs["edns"], outputs["dnssec"]) if a != b)
        # Should only differ in the EDNS flags fields (2 bytes per query * num_queries)
        if diff_count == 0:
            print("ERROR: EDNS and DNSSEC should differ in flags")
            return False
        print(f"INFO: EDNS and DNSSEC differ in {diff_count} bytes (flags fields)")
        
        print(f"SUCCESS: All flag combination tests passed for {input_path.name}")
        return True


def main():
    prechecks()
    
    all_passed = True
    for input_file in glob.glob("tests/*.txt"):
        input_path = pathlib.Path(input_file)
        gold_path = input_path.with_suffix('.gold')
        
        all_passed = all_passed and run_test(input_path, gold_path)
        all_passed = all_passed and test_flag_combinations(input_path)
    
    sys.exit(0 if all_passed else 1)


if __name__ == "__main__":
    main()
