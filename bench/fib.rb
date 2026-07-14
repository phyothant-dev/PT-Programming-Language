def fib(n)
  return n if n <= 1
  fib(n - 1) + fib(n - 2)
end

start = Time.now
result = fib(35)
elapsed = Time.now - start
puts "fib(35) = #{result}"
puts "Time: #{elapsed.round(4)}s"
