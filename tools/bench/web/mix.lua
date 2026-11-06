-- The request mix, weighted toward page renders as in a server-rendered app:
-- 45% the list page, 25% one todo's page, 10% the create form, 10% toggle
-- submits, 5% create submits (both answer 303 to a page), 5% the JSON list.
-- Ids fall in the seeded 1..100. done() prints one JSON line for the driver.
local counter = 0

setup = function(thread)
  counter = counter + 1
  thread:set("seed", counter)
end

init = function(args)
  math.randomseed(seed)
end

local form = {["Content-Type"] = "application/x-www-form-urlencoded"}
local back = {["Referer"] = "/"}

request = function()
  local r, id = math.random(100), math.random(100)
  if r <= 45 then
    return wrk.format("GET", "/")
  elseif r <= 70 then
    return wrk.format("GET", "/todos/" .. id)
  elseif r <= 80 then
    return wrk.format("GET", "/new")
  elseif r <= 90 then
    return wrk.format("POST", "/todos/" .. id .. "/toggle", back)
  elseif r <= 95 then
    return wrk.format("POST", "/todos", form, "title=buy+milk+%26+eggs")
  end
  return wrk.format("GET", "/api/todos")
end

done = function(summary, latency, requests)
  local e = summary.errors
  io.write(string.format('{"requests":%d,"duration_us":%d,"errors":%d,"p50_us":%d,"p99_us":%d}\n',
    summary.requests, summary.duration, e.connect + e.read + e.write + e.status + e.timeout,
    latency:percentile(50), latency:percentile(99)))
end
