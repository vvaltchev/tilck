# SPDX-License-Identifier: BSD-2-Clause
#
# Minimal code coverage reporter using Ruby's built-in Coverage module.
# No external gems required — generates an HTML report from Coverage.result.
#

require 'fileutils'
require 'pathname'
require 'cgi'

module CoverageReporter

  module_function

  # Filter coverage data to only pkgmgr source files (not tests).
  def filter_results(raw, source_dir)
    source_dir = File.realpath(source_dir)
    raw.select { |path, _|
      rp = File.realpath(path) rescue path
      rp.start_with?(source_dir) && !rp.include?("/tests/") &&
        !rp.end_with?("coverage_reporter.rb")
    }
  end

  # Compute per-file stats: { path => { total:, hit:, missed:, pct: } }
  def compute_stats(data)
    data.map { |path, info|
      lines = info[:lines]
      executable = lines.compact
      total = executable.length
      hit = executable.count { |n| n > 0 }
      missed = total - hit
      pct = total > 0 ? (hit * 100.0 / total).round(1) : 100.0
      [path, { total: total, hit: hit, missed: missed, pct: pct }]
    }.to_h
  end

  # Per-file BRANCH stats: { path => { total:, hit:, missed:, pct: } }
  #
  # Ruby reports branches as
  #
  #   { [:if, 0, 2, 2, 6, 5] => { [:then, 1, 3, 4, 3, 9] => 1,
  #                               [:else, 2, 5, 4, 5, 9] => 0 } }
  #
  # -- the outer key is the construct, the inner ones its arms and how
  # many times each was taken. An arm at 0 is a path the tests never
  # went down, and that is the thing line coverage cannot see: a line
  # counts as covered the first time it runs, with only one of its two
  # outcomes ever having happened. Most of the bugs this suite has
  # missed lived in the arm nobody took.
  def compute_branch_stats(data)
    data.map { |path, info|
      arms = (info[:branches] || {}).values.flat_map(&:values)
      total = arms.length
      hit = arms.count { |n| n > 0 }
      [path, { total: total, hit: hit, missed: total - hit,
               pct: total > 0 ? (hit * 100.0 / total).round(1) : 100.0 }]
    }.to_h
  end

  # Every arm never taken: [path, line, construct kind, arm kind].
  # This is the worklist -- each entry is a test that does not exist.
  def uncovered_branches(data)
    out = []

    data.each { |path, info|
      (info[:branches] || {}).each { |construct, arms|
        arms.each { |arm, count|
          out << [path, arm[2], construct[0], arm[0]] if count.zero?
        }
      }
    }

    return out.sort_by { |path, line, _, _| [path, line] }
  end

  def print_branch_summary(data, source_dir, top: 40)

    stats = compute_branch_stats(data)
    return if stats.values.sum { |s| s[:total] }.zero?

    puts
    puts "=" * 72
    puts "Ruby BRANCH coverage summary"
    puts "=" * 72

    total = 0
    hit = 0

    stats.sort_by { |p, s| [s[:pct], p] }.each do |path, s|
      next if s[:total].zero?
      rel = Pathname.new(path).relative_path_from(source_dir).to_s
      printf "  %-40s %5d / %5d  %5.1f%%  [%s]\n",
             rel, s[:hit], s[:total], s[:pct], s[:pct] >= 80 ? "OK" : "LOW"
      total += s[:total]
      hit += s[:hit]
    end

    pct = total > 0 ? (hit * 100.0 / total).round(1) : 100.0
    puts "-" * 72
    printf "  %-40s %5d / %5d  %5.1f%%\n", "TOTAL", hit, total, pct
    puts "=" * 72

    missed = uncovered_branches(data)
    return if missed.empty?

    puts
    puts "Branches never taken (#{missed.length}) -- each one is a"
    puts "test that does not exist:"
    puts

    missed.first(top).each { |path, line, construct, arm|
      rel = Pathname.new(path).relative_path_from(source_dir).to_s
      printf "  %-34s :%-5d %s -> %s\n", rel, line, construct, arm
    }

    if missed.length > top
      puts "  ... and #{missed.length - top} more"
    end

    puts
  end

  # Print a text summary to stdout.
  def print_summary(stats, source_dir)
    puts
    puts "=" * 72
    puts "Ruby code coverage summary"
    puts "=" * 72

    total_lines = 0
    total_hit = 0

    stats.sort_by { |p, _| p }.each do |path, s|
      rel = Pathname.new(path).relative_path_from(source_dir).to_s
      bar = s[:pct] >= 80 ? "OK" : "LOW"
      printf "  %-40s %5d / %5d  %5.1f%%  [%s]\n",
             rel, s[:hit], s[:total], s[:pct], bar
      total_lines += s[:total]
      total_hit += s[:hit]
    end

    total_pct = total_lines > 0 ?
      (total_hit * 100.0 / total_lines).round(1) : 100.0

    puts "-" * 72
    printf "  %-40s %5d / %5d  %5.1f%%\n",
           "TOTAL", total_hit, total_lines, total_pct
    puts "=" * 72
    puts
  end

  # Generate HTML report.
  def generate_html(data, stats, source_dir, output_dir)
    FileUtils.rm_rf(output_dir)
    FileUtils.mkdir_p(output_dir)

    total_lines = stats.values.sum { |s| s[:total] }
    total_hit = stats.values.sum { |s| s[:hit] }
    total_pct = total_lines > 0 ?
      (total_hit * 100.0 / total_lines).round(1) : 100.0

    # Generate per-file pages
    file_links = []
    stats.sort_by { |p, _| p }.each do |path, s|
      rel = Pathname.new(path).relative_path_from(source_dir).to_s
      html_name = rel.gsub("/", "_") + ".html"
      file_links << [rel, html_name, s]
      generate_file_page(path, data[path], s, rel, output_dir, html_name)
    end

    # Generate index page
    generate_index(file_links, total_hit, total_lines, total_pct, output_dir)
    puts "Coverage report: #{output_dir}/index.html"
  end

  def generate_index(file_links, total_hit, total_lines, total_pct, output_dir)
    rows = file_links.map { |rel, html, s|
      color = pct_color(s[:pct])
      "<tr>" \
        "<td><a href=\"#{html}\">#{h(rel)}</a></td>" \
        "<td style=\"color:#{color}\">#{s[:pct]}%</td>" \
        "<td>#{s[:hit]} / #{s[:total]}</td>" \
      "</tr>"
    }.join("\n")

    html = <<~HTML
      <!DOCTYPE html>
      <html><head><meta charset="utf-8">
      <title>Ruby pkgmgr coverage</title>
      <style>
        body { font-family: monospace; margin: 2em; }
        table { border-collapse: collapse; }
        th, td { padding: 4px 12px; text-align: left; border-bottom: 1px solid #ddd; }
        th { background: #f0f0f0; }
        .summary { font-size: 1.2em; margin: 1em 0; }
      </style></head><body>
      <h1>Ruby pkgmgr — code coverage</h1>
      <p class="summary">Total: <b>#{total_hit} / #{total_lines}</b> lines
        (<b style="color:#{pct_color(total_pct)}">#{total_pct}%</b>)</p>
      <table>
      <tr><th>File</th><th>Coverage</th><th>Lines</th></tr>
      #{rows}
      </table></body></html>
    HTML

    File.write("#{output_dir}/index.html", html)
  end

  def generate_file_page(path, info, stats, rel, output_dir, html_name)
    lines_data = info[:lines]
    source = File.read(path)
    source_lines = source.lines

    body = source_lines.each_with_index.map { |line, i|
      cov = lines_data[i]
      cls = cov.nil? ? "non" : (cov > 0 ? "hit" : "miss")
      count = cov.nil? ? "" : cov.to_s
      "<tr class=\"#{cls}\">" \
        "<td class=\"ln\">#{i + 1}</td>" \
        "<td class=\"ct\">#{count}</td>" \
        "<td class=\"code\">#{h(line.chomp)}</td>" \
      "</tr>"
    }.join("\n")

    html = <<~HTML
      <!DOCTYPE html>
      <html><head><meta charset="utf-8">
      <title>#{h(rel)}</title>
      <style>
        body { font-family: monospace; margin: 1em; }
        table { border-collapse: collapse; }
        td { padding: 0 6px; white-space: pre; }
        .ln { color: #999; text-align: right; border-right: 1px solid #ddd;
              user-select: none; }
        .ct { color: #666; text-align: right; min-width: 3em; }
        .hit  td.code { background: #dfd; }
        .miss td.code { background: #fdd; }
        .non  td.code { background: #fff; }
        h2 { margin-bottom: 0.5em; }
        .summary { margin-bottom: 1em; }
      </style></head><body>
      <h2><a href="index.html">&larr; index</a> &mdash; #{h(rel)}</h2>
      <p class="summary">#{stats[:hit]} / #{stats[:total]} lines
        (<b style="color:#{pct_color(stats[:pct])}">#{stats[:pct]}%</b>)</p>
      <table>#{body}</table>
      </body></html>
    HTML

    File.write("#{output_dir}/#{html_name}", html)
  end

  def h(s) = CGI.escapeHTML(s.to_s)

  def pct_color(pct)
    pct >= 80 ? "#070" : (pct >= 50 ? "#a60" : "#c00")
  end

  # Main entry point. Call after tests finish with Coverage.result.
  def report(raw_result, source_dir: nil, output_dir: "coverage_html")
    source_dir ||= File.expand_path("..", __dir__)
    data = filter_results(raw_result, source_dir)
    stats = compute_stats(data)
    print_summary(stats, Pathname.new(source_dir))
    print_branch_summary(data, Pathname.new(source_dir))
    generate_html(data, stats, source_dir, output_dir)
  end
end
