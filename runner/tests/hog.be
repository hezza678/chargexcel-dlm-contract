# Grows without bound: the arena must run out and fail the tick, not the unit.
def tick()
  var l = []
  while true l.push("some text that takes up room " + str(size(l))) end
end
