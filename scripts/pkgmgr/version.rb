# SPDX-License-Identifier: BSD-2-Clause


module VersionType

  DOT                = :dot          # 1.2.11
  UNDERSCORE         = :_            # 1_2_11
  R_UNDERSCORE       = :r_           # R1_2_11
  V_DOT              = :vdot         # v1.2.11
  FLAT_DATE          = :flatdate     # 20240726
  UNDERSC_DATE       = :_date        # 2024_12_12
  R_UNDERSC_DATE     = :r_date       # R2024_12_12
  SHORT_DOT_DATE     = :shortdate    # 2024.04
  SHORT_UNDERSC_DATE = :short_date   # 2024_04
  DOT_PRE            = :dotpre       # 2.2.alpha.7, 1.0-rc1
  HASH               = :hash         # dbcefc6

end

module VersionBaseType
  REG                = :reg
  DATE               = :date
  SHORT_DATE         = :shortdate
  HASH               = :hash
end

def get_base_version_type(t)
  case t
    when VersionType::DOT,
         VersionType::UNDERSCORE,
         VersionType::R_UNDERSCORE,
         VersionType::V_DOT,
         VersionType::DOT_PRE

      VersionBaseType::REG

    when VersionType::FLAT_DATE,
         VersionType::R_UNDERSC_DATE,

      VersionBaseType::DATE

    when VersionType::SHORT_DOT_DATE,
         VersionType::SHORT_UNDERSC_DATE

      VersionBaseType::SHORT_DATE

    when VersionType::HASH

      VersionBaseType::HASH
  end
end

class Version

  HASH_MIN_DIGITS = 6

  #
  # Prerelease ordering.
  #
  # A prerelease sorts BEFORE the release it leads to -- 2.2.alpha.7 <
  # 2.2.beta.1 < 2.2 -- which is the whole reason these cannot just be
  # parsed as another dotted sequence with a word in it. FINAL is the
  # rank a version with no prerelease part carries, so that one
  # comparison handles both cases without a special branch.
  #
  PRE_RANKS  = { "alpha" => 0, "beta" => 1, "rc" => 2 }.freeze
  PRE_FINAL  = [3, 0].freeze
  PRE_REGEX  = /\Av?(\d+(?:\.\d+)*)[.\-](alpha|beta|rc)[.\-]?(\d*)\z/

  protected
  attr_reader :orig_str

  public
  attr_reader :comps, :type, :pre

  def initialize(ver_str)

    if ver_str.is_a?(Version)
      @comps = ver_str.comps
      @type = ver_str.type
      @pre = ver_str.pre
      @orig_str = ver_str.orig_str
      freeze
      return
    end

    if !ver_str
      raise ArgumentError, "empty string"
    end

    if !ver_str.is_a?(String)
      raise ArgumentError, "not a string: #{ver_str}"
    end

    # Save the original version string.
    @orig_str = ver_str

    if ver_str.match? /\A\d+\z/ and ver_str.length == 8
      parse_8_digit_number(ver_str)
    elsif ver_str.match? PRE_REGEX
      parse_prerelease(ver_str)
    elsif ver_str.match? /\Av?\d+(?:\.\d+)+\z/
      parse_dot_sequence(ver_str)
    elsif ver_str.match? /\AR?\d+(?:_\d+)+\z/
      parse_underscore_sequence(ver_str)
    elsif ver_str.match? /\A[0-9a-f]+\z/
      parse_hex(ver_str)
    else
      raise ArgumentError, "Unrecognized version string: #{ver_str}"
    end

    freeze
  end

  def <=>(other)
    if other.is_a?(Version)

      t1 = get_base_version_type(@type)
      t2 = get_base_version_type(other.type)

      if t1 != t2
        raise "Cannot compare versions #{to_s} and #{other.to_s}"
      end

      if t1 == VersionType::HASH
        return @orig_str <=> other.orig_str
      end

      # Drop the trailing 0s, so that 1.2 == 1.2.0. Done even when the
      # lengths match, because a prerelease's release part is compared
      # against a final version's: 2.2.alpha.7 carries [2,2] and has to
      # line up with 2.2.0's [2,2,0] before the ranks below decide.
      c1 =      @comps.reverse.drop_while(&:zero?).reverse
      c2 = other.comps.reverse.drop_while(&:zero?).reverse
      cmp = c1 <=> c2
      return cmp if cmp != 0

      # Same release: a prerelease loses to the release itself, and to
      # any later prerelease of it.
      return pre_key <=> other.pre_key
    end

    if other.is_a?(String)
      return other == 'ALL' ? 0 : self <=> Version.new(other)
    end

    return nil
  end

  def eql?(other)
    other.is_a?(Version) and @orig_str == other.orig_str
  end

  def <(other)  = (check_ordered; (self <=> other)  < 0)
  def <=(other) = (check_ordered; (self <=> other) <= 0)
  def >(other)  = (check_ordered; (self <=> other)  > 0)
  def >=(other) = (check_ordered; (self <=> other) >= 0)
  def ==(other) = ((self <=> other) == 0)
  def hash = @type != VersionType::HASH ? [@comps, @pre].hash : @orig_str.hash
  def prerelease? = !@pre.nil?

  protected
  # The prerelease rank used for ordering; a final release outranks
  # every prerelease of the same version.
  def pre_key = @pre || PRE_FINAL

  public
  def blank? = false

  def serialize
    case @type
      when VersionType::DOT          then @comps.join(".")
      when VersionType::V_DOT        then "v" + @comps.join(".")
      when VersionType::UNDERSCORE   then @comps.join("_")
      when VersionType::R_UNDERSCORE then "R" + @comps.join("_")
      when VersionType::FLAT_DATE
        [@comps[0], '%02d' % @comps[1], '%02d' % @comps[2]].join()

      when VersionType::UNDERSC_DATE
        [@comps[0], '%02d' % @comps[1], '%02d' % @comps[2]].join("_")

      when VersionType::R_UNDERSC_DATE
        "R" + [@comps[0], '%02d' % @comps[1], '%02d' % @comps[2]].join("_")

      when VersionType::SHORT_DOT_DATE
        [@comps[0], '%02d' % @comps[1]].join(".")

      when VersionType::SHORT_UNDERSC_DATE
        [@comps[0], '%02d' % @comps[1]].join("_")

      when VersionType::DOT_PRE
        @orig_str

      when VersionType::HASH
        @orig_str
    end
  end

  def to_s = @orig_str
  def _
    assert { [VersionType::DOT, VersionType::UNDERSCORE].include? @type }
    @comps.join("_")
  end
  def to_dot
    assert { get_base_version_type(@type) == VersionBaseType::REG }

    if prerelease?
      raise TypeError,
            "#{@orig_str} is a prerelease: to_dot would silently drop it"
    end

    return Ver(@comps.join("."))
  end

  def ordered = @type != VersionType::HASH

  private
  def check_ordered
    if !ordered
      raise TypeError, "Cannot perform <,<=,>,>= check on: #{orig_str}"
    end
  end

  def parse_8_digit_number(s)
    y = s[0..3].to_i
    m = s[4..5].to_i
    d = s[6..7].to_i

    if y in (2010..2099) and m in (1..12) and d in (1..31)
      @comps = [y, m, d]
      @type = VersionType::FLAT_DATE
    else
      # 8 digits that don't form a valid date (e.g. 99999999)
      # are treated as a hash — always >= HASH_MIN_DIGITS (6).
      @comps = [s.to_i]
      @type = VersionType::HASH
    end
  end

  # 2.2.alpha.7, 2.2.beta, 1.0-rc1: a dotted release part, then a
  # prerelease word and an optional number. Upstreams spell the
  # separator both ways, and glycin ships nothing BUT prereleases, so
  # this is not an exotic case to skip.
  def parse_prerelease(s)
    m = PRE_REGEX.match(s)
    @type = VersionType::DOT_PRE
    @comps = m[1].split(".").map(&:to_i)
    @pre = [PRE_RANKS.fetch(m[2]), m[3].to_i]
  end

  def parse_dot_sequence(s)
    if s[0] == 'v'

      s = s[1..]
      @type = VersionType::V_DOT

    else
      @type = VersionType::DOT

      if s.length == 7
        y = s[0..3].to_i
        m = s[5..6].to_i
        if y in (2010..2099) and m in (1..12)
          @type = VersionType::SHORT_DOT_DATE
        end
      end
    end

    @comps = s.split(".").map(&:to_i)
  end

  def parse_underscore_sequence(s)
    if s[0] == 'R'
      s = s[1..]
      @type = VersionType::R_UNDERSCORE
    else
      @type = VersionType::UNDERSCORE
    end

    if s.length == 10
      y = s[0..3].to_i
      m = s[5..6].to_i
      d = s[8..9].to_i
      if y in (2010..2099) and m in (1..12) and d in (1..31)
        if @type == VersionType::R_UNDERSCORE
          @type = VersionType::R_UNDERSC_DATE
        else
          @type = VersionType::UNDERSC_DATE
        end
      end

    elsif s.length == 7
      y = s[0..3].to_i
      m = s[5..6].to_i
      if y in (2010..2099) and m in (1..12)
        @type = VersionType::SHORT_UNDERSC_DATE
      end
    end

    @comps = s.split("_").map(&:to_i)
  end

  def parse_hex(s)
    if s.match? /\A[0-9]+\z/ and s.length < HASH_MIN_DIGITS
      @type = VersionType::DOT
      @comps = [s.to_i]
    else
      @type = VersionType::HASH
    end
  end
end

def Ver(s)
  return !s.nil? ? Version.new(s) : nil
end

def SafeVer(s)
  return Ver(s)
rescue StandardError => e
  return nil
end

