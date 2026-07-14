start = Time.now
arr = (0...100_000).map { |i| i * 2 }
s = arr.sum
elapsed = Time.now - start
puts "Sum: #{s}"
puts "Time: #{elapsed.round(4)}s"
