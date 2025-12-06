# SPDX-License-Identifier: BSD-2-Clause

class Version

  attr_reader :comps

  def initialize(ver_str)

    if ver_str.is_a?(Version)
      @comps = ver_str.comps
      freeze
      return
    end

    if !ver_str
      raise ArgumentError, "empty string"
    end

    if !ver_str.is_a?(String)
      raise ArgumentError, "not a string: #{ver_str}"
    end

    if !ver_str.match?(/\A\d+(?:\.\d+)+\z/)
      raise ArgumentError, "not a version string: #{ver_str}"
    end

    @comps = ver_str.split(".").map(&:to_i)
    freeze
  end

  def <=>(other)
    if other.is_a?(Version)
      return @comps <=> other.comps
    end

    if other.is_a?(String)
      return self <=> Version.new(other)
    end

    return nil
  end

  def eql?(other)
    other.is_a?(Version) && @comps == other.comps
  end

  def <(other)  = ((self <=> other)  < 0)
  def <=(other) = ((self <=> other) <= 0)
  def >(other)  = ((self <=> other)  > 0)
  def >=(other) = ((self <=> other) >= 0)
  def ==(other) = ((self <=> other) == 0)
  def hash = @comps.hash
  def to_s = @comps.join(".")

  def _
    @comps.join("_")
  end
end

def Ver(s)
  Version.new(s)
end
