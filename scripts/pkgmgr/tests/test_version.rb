# SPDX-License-Identifier: BSD-2-Clause

require_relative 'test_helper'

class TestVersionClass < Minitest::Test

  def test_dot
    v = Ver("1.2.3")
    assert_equal(VersionType::DOT, v.type)
    assert_equal([1,2,3], v.comps)
    assert_equal("1.2.3", v.serialize())
    assert(v.ordered)

    v = Ver("1.2")
    assert_equal(VersionType::DOT, v.type)
    assert_equal([1,2], v.comps)
    assert_equal("1.2", v.serialize())
    assert(v.ordered)

    v = Ver("1.02.3")
    assert_equal(VersionType::DOT, v.type)
    assert_equal([1,2,3], v.comps)
    assert_equal("1.2.3", v.serialize())
    assert_equal("1.02.3", v.to_s())
    assert(v.ordered)

    v = Ver("v1.02")
    assert_equal(VersionType::V_DOT, v.type)
    assert_equal([1,2], v.comps)
    assert_equal("v1.2", v.serialize())
    assert_equal("v1.02", v.to_s())
    assert(v.ordered)
  end

  def test_flat_number
    v = Ver("123")
    assert_equal(VersionType::DOT, v.type)
    assert_equal([123], v.comps)
    assert_equal("123", v.serialize())
    assert(v.ordered)

    v = Ver("12345678")
    assert_equal(VersionType::HASH, v.type)
    assert_equal([12345678], v.comps)
    assert_equal("12345678", v.serialize())
    assert(!v.ordered)
  end

  def test_comparison
    assert_operator(Ver("1.2"), :==, Ver("1.2"))
    assert_operator(Ver("1.2"), :==, Ver("1.02"))
    assert_operator(Ver("1.2"), :==, Ver("01.2"))
    assert_operator(Ver("1.2"), :<, Ver("1.2.1"))
    assert_operator(Ver("1.2"), :==, Ver("1.2.0"))
    assert_operator(Ver("1.9"), :<, Ver("2.0"))
    assert_operator(Ver("1.2"), :<=, Ver("1.2"))
    assert_operator(Ver("1.2"), :<=, Ver("1.3"))
    refute_operator(Ver("1.2"), :<=, Ver("1.1"))
    assert_operator(Ver("1.2"), :>, Ver("1.1"))
    assert_operator(Ver("1.2"), :>=, Ver("1.1"))
    assert_operator(Ver("1.2"), :>=, Ver("1.2"))
    refute_operator(Ver("1.2"), :>=, Ver("1.2.1"))
  end

  def test_underscore
    v = Ver("1_2_3")
    assert_equal(VersionType::UNDERSCORE, v.type)
    assert_equal([1,2,3], v.comps)
    assert_equal("1_2_3", v.serialize())
    assert(v.ordered)

    v = Ver("R1_2_3")
    assert_equal(VersionType::R_UNDERSCORE, v.type)
    assert_equal([1,2,3], v.comps)
    assert_equal("R1_2_3", v.serialize())
    assert(v.ordered)

    v = Ver("R1500_12_12")
    assert_equal(VersionType::R_UNDERSCORE, v.type)
    assert_equal([1500, 12, 12], v.comps)
    assert_equal("R1500_12_12", v.serialize())
    assert(v.ordered)

    v = Ver("R1500_13_12")
    assert_equal(VersionType::R_UNDERSCORE, v.type)
    assert_equal([1500, 13, 12], v.comps)
    assert_equal("R1500_13_12", v.serialize())
    assert(v.ordered)

    v = Ver("R1500_12_32")
    assert_equal(VersionType::R_UNDERSCORE, v.type)
    assert_equal([1500, 12, 32], v.comps)
    assert_equal("R1500_12_32", v.serialize())
    assert(v.ordered)
  end

  def test_undesc_date
    v = Ver("2024_12_12")
    assert_equal(VersionType::UNDERSC_DATE, v.type)
    assert_equal([2024, 12, 12], v.comps)
    assert_equal("2024_12_12", v.serialize())
    assert(v.ordered)
  end

  def test_r_undesc_date
    v = Ver("R2024_12_12")
    assert_equal(VersionType::R_UNDERSC_DATE, v.type)
    assert_equal([2024, 12, 12], v.comps)
    assert_equal("R2024_12_12", v.serialize())
    assert(v.ordered)
  end

  def test_flat_date
    v = Ver("20240726")
    assert_equal(VersionType::FLAT_DATE, v.type)
    assert_equal([2024, 07, 26], v.comps)
    assert_equal("20240726", v.serialize())
    assert(v.ordered)
  end

  def test_short_date
    v = Ver("2012.04")
    assert_equal(VersionType::SHORT_DOT_DATE, v.type)
    assert_equal([2012, 4], v.comps)
    assert_equal("2012.04", v.serialize())
    assert(v.ordered)

    v = Ver("2012_04")
    assert_equal(VersionType::SHORT_UNDERSC_DATE, v.type)
    assert_equal([2012, 4], v.comps)
    assert_equal("2012_04", v.serialize())
    assert(v.ordered)
  end

  def test_hash
    v = Ver("a12f")
    assert_equal(VersionType::HASH, v.type)
    assert_equal("a12f", v.serialize())
    refute(v.ordered)
    assert_operator(Ver("a12f"), :==, Ver("a12f"))
    refute_operator(Ver("a12f"), :!=, Ver("a12f"))
    assert_operator(Ver("a12f"), :!=, Ver("a13f"))

    v = Ver("12345")
    assert_equal(VersionType::DOT, v.type)

    v = Ver("123456")
    assert_equal(VersionType::HASH, v.type)
  end

  def test_invalid
    assert_raises(ArgumentError) { Ver("") }
    assert_raises(ArgumentError) { Ver("zazz") }
  end

  def test_invalid_comparison
    assert_raises(TypeError) { Ver("a12") < Ver("a13") }
    assert_raises(TypeError) { Ver("a12") <= Ver("a13") }
    assert_raises(TypeError) { Ver("a12") > Ver("a13") }
    assert_raises(TypeError) { Ver("a12") >= Ver("a13") }
  end
end
