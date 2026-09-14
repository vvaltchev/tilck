# SPDX-License-Identifier: BSD-2-Clause
#
# pmpins.rb -- the registry's side of pmpins.
#
# `list` (the default): one line per cache file the registry can
# fetch on this host,
#
#   <mode> <name> <url> <remote> <tag>
#
# where mode is `git` or `http`, remote the remote file name (http)
# or `-`, tag the git tag (git) or `-`. The bash side reads it to
# fetch each file fresh and to ask the upstream what a tag is.
#
# `refetch-git`: every cloned source fetched again through the
# package manager's own path (SourceRef#download -> Cache), which
# checks the clone against its pin, packs it without .git and records
# it in cache/.hashes. Nothing is built.
#
require_relative '../../pkgmgr/main'

Main.read_gcc_ver_defaults

sources = pkgmgr.source_files.sort.map { |name, users|
  _, ver, src = users.first
  [name, ver, src]
}

case ARGV[0]
when "refetch-git"
  ok = true
  for name, ver, src in sources do
    next if !src.fetch_via_git? || name != src.tarname(ver)
    puts "=== #{name}"
    ok &= src.download(ver)
  end
  exit(ok ? 0 : 1)
else
  for name, ver, src in sources do
    if src.fetch_via_git? && name == src.tarname(ver)
      puts "git #{name} #{src.url} - #{src.git_tag(ver)}"
    elsif name == src.tarname(ver)
      puts "http #{name} #{src.url} #{src.remote_tarname(ver)} -"
    else
      e = src.extra_files.find { |x| x[:file] == name }
      puts "http #{name} #{e[:url]} #{e[:file]} -"
    end
  end
end
