# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'term'

require 'singleton'

class PackageManager

  include Singleton
  attr_reader :packages

  def initialize
    @packages = {}
    @config_versions = read_config_versions()
  end

  def register(package)
    if !package.is_a?(Package)
      raise ArgumentError
    end

    if @packages.include? package.id
      raise NameError, "package #{package.name} already registered"
    end

    @packages[package.id] = package
  end

  def get(name, ver = nil)
    return @packages[name]
  end

  def get_tc(arch, ver = nil)
    return get("gcc_#{arch}_musl", ver)
  end

  def get_config_ver(name)
    return @config_versions[name]
  end

  def show_status_all

    puts "--- GCC toolchains (pre-compiled) ---"
    @packages.values.select { |p| p.is_compiler }.each do |p|
      show_status(p)
    end

    puts
    puts "--- Packages for GCC: #{ARCH.gcc_ver} ---"
    @packages.values.select { |p| !p.is_compiler }.each do |p|
      show_status(p)
    end
  end

  private
  def read_config_versions
    result = {}
    data = File.read(MAIN_DIR / "other" / "pkg_versions")

    for line in data.split("\n")
      if !line.start_with? "VER_"
        raise "Invalid line in pkg_versions: #{line}"
      end

      line = line.sub("VER_", "")
      key, value = line.split("=")

      if key.blank? || value.blank?
        raise "Invalid line in pkg_versions: #{line}"
      end

      if result[key]
        raise "Duplicate key in pkg_versions: #{key}"
      end

      result[key] = Ver(value)
    end

    return result
  end

  def show_status(pkg)

    def install_arch_str(info) =
      info.on_host ? "host" : info.arch.name

    def add_braces(s) = "{#{s}}"

    list = pkg.install_list
    name = pkg.name

    if list.empty?
      puts "#{name.ljust(35)} [ #{Package::EMPTY_STR} ]"
      return
    end

    # Exclude installations for other host archs
    list.filter! { |x| x.arch == HOST_ARCH }

    # Get an unique list of archs from all the installations
    archs = list.map{|e| install_arch_str(e)}.uniq

    s = archs.map {
      |a|
      [
        a,
        add_braces(
          list.filter {
            |e| install_arch_str(e) == a
          }.map(&:ver).map(&:to_s).join(", ")
        )
      ].join(": ")
    }.join(", ")
    puts "#{name.ljust(35)} [ #{Package::INSTALLED_STR} ] [ #{s} ]"
  end
end

def pkgmgr = PackageManager.instance
