#!/usr/bin/env python3

import glob
import pathlib
import shutil
import subprocess
import sys
import tempfile
from typing import List


def require(condition, on_success=None, on_failure="Assertion failed"):
    """Assert condition and print message based on result."""
    if condition:
        if on_success:
            print(f"SUCCESS: {on_success}")
    else:
        assert False, f"ERROR: {on_failure}"


def count_bit_differences(data1: bytes, data2: bytes) -> int:
    """Count the number of differing bits between two byte strings."""
    assert len(data1) == len(data2), "Data must be same length"
    return sum((a ^ b).bit_count() for a, b in zip(data1, data2))


def prechecks():
    """Check if dnscvt binary exists and is executable."""
    script_dir = pathlib.Path(__file__).parent
    dnscvt_path = script_dir / 'dnscvt'
    
    assert dnscvt_path.exists(), "dnscvt binary not found"
    assert dnscvt_path.is_file(), "dnscvt is not a regular file"


def run_test(input_path: pathlib.Path, gold_path: pathlib.Path, args: List[str] = None):
    """Test dnscvt by comparing output with gold standard."""
    if args is None:
        args = []
    script_dir = pathlib.Path(__file__).parent
    dnscvt_path = script_dir / 'dnscvt'
    
    assert input_path.exists(), f"{input_path} not found"
    assert gold_path.exists(), f"{gold_path} not found"
    
    with tempfile.TemporaryDirectory() as temp_dir:
        temp_dir_path = pathlib.Path(temp_dir)
        temp_output = temp_dir_path / "output.raw"
        
        cmd = [str(dnscvt_path), *args, "-o", str(temp_output), str(input_path)]
        result = subprocess.run(cmd, capture_output=True, text=True)
        assert result.returncode == 0, f"dnscvt failed: {result.stderr}"
        assert temp_output.exists(), f"{temp_output} was not created"
        
        with open(temp_output, 'rb') as f1, open(gold_path, 'rb') as f2:
            output_content = f1.read()
            gold_content = f2.read()
            
            require(output_content == gold_content, 
                   on_success=f"{input_path.name} -> output.raw matches {gold_path.name}",
                   on_failure=f"Output does not match gold standard. Generated: {len(output_content)} bytes, Gold: {len(gold_content)} bytes")
        
        return True


def test_flag_combinations(input_path: pathlib.Path):
    """Test dnscvt flag combinations and verify expected behavior."""
    script_dir = pathlib.Path(__file__).parent
    dnscvt_path = script_dir / 'dnscvt'
    
    require(input_path.exists(), on_failure=f"{input_path} not found")
    
    with tempfile.TemporaryDirectory() as temp_dir:
        temp_dir_path = pathlib.Path(temp_dir)
        
        def run_dnscvt(output_file, *args):
            cmd = [str(dnscvt_path), *args, "-o", str(output_file), str(input_path)]
            result = subprocess.run(cmd, capture_output=True, text=True)
            require(result.returncode == 0, on_failure=f"dnscvt failed: {result.stderr}")
            require(output_file.exists(), on_failure=f"{output_file} was not created")
            
            with open(output_file, 'rb') as f:
                return f.read()
        
        # Count queries in input file
        with open(input_path, 'r') as f:
            lines = [line.strip() for line in f if line.strip()]
            num_queries = len(lines)
        
        # Test all flag combinations
        no_flags = run_dnscvt(temp_dir_path / "no_flags.raw")
        edns = run_dnscvt(temp_dir_path / "edns.raw", "-e")
        dnssec = run_dnscvt(temp_dir_path / "dnssec.raw", "-D")
        edns_dnssec = run_dnscvt(temp_dir_path / "edns_dnssec.raw", "-e", "-D")
        
        # Verify size relationships
        base_size = len(no_flags)
        edns_size = len(edns)
        dnssec_size = len(dnssec)
        
        expected_edns_increase = 11 * num_queries  # 11 bytes per EDNS OPT RR
        actual_edns_increase = edns_size - base_size
        require(actual_edns_increase == expected_edns_increase,
               on_success=f"EDNS adds {actual_edns_increase} bytes ({num_queries} queries × 11 bytes)",
               on_failure=f"EDNS size increase mismatch: expected {expected_edns_increase} bytes ({num_queries} × 11), got {actual_edns_increase}")
        
        require(dnssec_size == edns_size,
               on_success=f"DNSSEC size matches EDNS size ({dnssec_size} bytes)",
               on_failure=f"DNSSEC size ({dnssec_size}) should equal EDNS size ({edns_size})")
        
        require(edns_dnssec == dnssec,
               on_success="-e -D produces same output as -D (precedence working)",
               on_failure="-e -D should produce same output as -D")
        
        diff_bits = count_bit_differences(edns, dnssec)
        expected_diff_bits = num_queries  # 1 DO bit per query
        require(diff_bits == expected_diff_bits,
               on_success=f"EDNS and DNSSEC differ in {diff_bits} bits ({num_queries} queries × 1 DO bit)",
               on_failure=f"EDNS/DNSSEC bit difference mismatch: expected {expected_diff_bits} bits ({num_queries} × 1), got {diff_bits}")
        
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
