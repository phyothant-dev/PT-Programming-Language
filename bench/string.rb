start = Time.now
s = ""
100_000.times { s += "x" }
elapsed = Time.now - start
puts "Length: #{s.length}"
puts "Time: #{elapsed.round(4)}s"
