# SPDX-License-Identifier: BSD-2-Clause
#
# WHAT EVERY CACHED FILE WAS WHEN IT WAS PLACED: cache/.hashes.
#
# other/pkg_hashes says what a source must be; this table says what
# the bytes under a name were the moment the package manager put
# them there, checked and found right. The two answer different
# questions. A file whose digest is not the one recorded here has
# been damaged since -- a disk, a copy, a partial protocol that is
# not ours -- and is set aside and fetched again. A file that is
# what was recorded but not what the pin names has not moved: the
# pin has, or the file was placed before pins existed.
#
# A table of names (table.rb): a name is a cache filename, the
# value its sha256 as SourcePins spells one. Ours to regenerate, so
# a damaged table is dropped with a warning rather than refused; a
# name whose file is gone is dropped at the next write.
#
require_relative 'early_logic'
require_relative 'table'
require_relative 'lock'

module Cache
  module Hashes

    FILE = ".hashes"

    module_function

    def path = TC_CACHE / FILE

    # {name => "sha256:..."}: empty where there is no table, or the
    # one there is cannot be read.
    def read
      return Table.read(path) || {}
    rescue Table::Malformed => e
      warning "#{path}: #{e.message}: ignoring the table"
      return {}
    end

    def of(name) = read[name]

    def record(name, digest)
      update { |t| t[name] = digest.to_s }
    end

    def forget(name)
      update { |t| t.delete(name) }
    end

    # Read, changed, written back, as one process's step: two
    # recording at once would otherwise keep only one's entry.
    def update
      Lock.held(Cache.locks_dir, "hashes", what: "the cache's record") do
        t = read
        yield t
        t.delete_if { |name, _| !(TC_CACHE / name).file? }
        Table.write(path, t)
      end
    end

    # Where a cached file stands, from what is known of it:
    #
    #   :ok        its bytes are the recorded ones, and the pin names them
    #   :damaged   its bytes moved since it was placed
    #   :unpinned  nothing to hold it to
    #   :other     intact, but not what the pin names
    #
    # `digest` is the file's sha256 now, `recorded` this table's
    # word on it (nil where it has none), `ref` the commit a packed
    # clone says it came from (nil where it says nothing), `pin`
    # what other/pkg_hashes names (nil where it names nothing).
    def judge(pin:, digest:, recorded:, ref:)
      return :damaged if recorded && recorded != digest.to_s
      return :unpinned if pin.nil?
      named = pin.kind == :git ? ref == pin.value : digest.to_s == pin.to_s
      return named ? :ok : :other
    end
  end
end
