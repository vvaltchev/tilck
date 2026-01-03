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
end


