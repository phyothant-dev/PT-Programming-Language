start = Time.now
s = (1..10_000_000).sum
elapsed = Time.now - start
puts "Sum: #{s}"
puts "Time: #{elapsed.round(4)}s"
