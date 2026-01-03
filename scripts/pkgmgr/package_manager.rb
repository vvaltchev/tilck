# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'term'

require 'singleton'

class PackageManager

  include Singleton

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

  def get(name, on_host = false, ver = nil)
    return @packages[[name, on_host]]
  end

  def get_tc(arch, ver = nil)
    return get("gcc_#{arch}_musl", true, ver)
  end

  def show_status_all
    for id, p in @packages do
      p.show_status
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

end


