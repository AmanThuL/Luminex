from __future__ import annotations

import unittest

from Tools.check_checkpoint_a import check_split, parse_case_names


class CheckSplitTests(unittest.TestCase):
    """`check_split` compares two binaries' tagged case names against the frozen inventory."""

    def test_matching_split_passes(self) -> None:
        errors = check_split(
            tests=["b", "a"],
            rhi_tests=["c", "d"],
            union_inventory=["a", "b", "c", "d"],
            rhi_inventory=["c", "d"],
        )
        self.assertEqual(errors, [])

    def test_a_case_tagged_in_both_binaries_fails(self) -> None:
        errors = check_split(
            tests=["a", "shared"],
            rhi_tests=["shared", "c"],
            union_inventory=["a", "shared", "c"],
            rhi_inventory=["shared", "c"],
        )
        self.assertTrue(any("shared" in error and "both" in error for error in errors))

    def test_a_frozen_name_missing_from_both_binaries_fails(self) -> None:
        errors = check_split(
            tests=["a"],
            rhi_tests=["c"],
            union_inventory=["a", "b", "c"],
            rhi_inventory=["c"],
        )
        self.assertTrue(any("b" in error for error in errors))

    def test_a_tagged_case_outside_the_frozen_union_fails(self) -> None:
        errors = check_split(
            tests=["a", "surprise"],
            rhi_tests=["c"],
            union_inventory=["a", "c"],
            rhi_inventory=["c"],
        )
        self.assertTrue(any("surprise" in error for error in errors))

    def test_an_rhi_half_mismatch_fails(self) -> None:
        errors = check_split(
            tests=["a"],
            rhi_tests=["c", "d"],
            union_inventory=["a", "c", "d"],
            rhi_inventory=["c"],
        )
        self.assertTrue(any("d" in error for error in errors))


class ParseCaseNamesTests(unittest.TestCase):
    """`parse_case_names` reads Catch2's `--list-tests --reporter xml` output."""

    def test_parses_every_name_element(self) -> None:
        xml = (
            "<MatchingTests>"
            "<TestCase><Name>first case</Name></TestCase>"
            "<TestCase><Name>second case</Name></TestCase>"
            "</MatchingTests>"
        )
        self.assertEqual(parse_case_names(xml), ["first case", "second case"])

    def test_an_empty_listing_parses_to_no_names(self) -> None:
        self.assertEqual(parse_case_names("<MatchingTests/>"), [])


if __name__ == "__main__":
    unittest.main()
