#!/usr/bin/env ruby
def prnthelp
  puts "Hello sir, what would you like to do?"
  puts "1: dir"
  puts "2: exit"
end

def loop
  prnthelp
  case gets.chomp.to_i
    when 1 then puts "you chose dir!"
    when 2 then puts "you chose exit!"
      exit
  end
  loop
end

loop
