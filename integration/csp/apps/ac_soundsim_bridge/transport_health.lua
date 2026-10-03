-- Producer health, not a CSP consumer cursor or an acoustic acceptance score.
local M={}
function M.new() return {} end
function M.check(h,r,now)
  local key=tostring(r.generation)..':'..r.audioName..':'..tostring(r.resets or 0)
  if key~=h.key then
    h.key,h.frames,h.late,h.progressAt,h.windowAt,h.windowLate,h.reason=key,r.frames,r.late,now,now,r.late,nil
  end
  if r.modeID~=1 then
    h.progressAt,h.windowAt,h.windowLate=now,now,r.late
    return false,'runtime not running'
  end
  if h.reason then return false,h.reason end
  if r.frames<h.frames or r.late<h.late then
    h.reason='producer counters regressed without a new session'
  elseif r.frames>h.frames then h.progressAt=now
  elseif now-h.progressAt>0.3 then h.reason='producer PCM stalled (>300 ms)' end
  -- Defensive cutoff, NOT an acceptable dropout budget: fifteen missing6.667ms
  -- blocks in a roughly one-second observed window is gross cadence failure.
  -- Latch until producer/engine reset or explicit retry; no repeated reacquire.
  local window=now-h.windowAt
  if window>=1 then
    if r.late-h.windowLate>=15*window then h.reason='producer cadence failure (>=100 ms missing audio/s)' end
    h.windowAt,h.windowLate=now,r.late
  end
  h.frames,h.late=r.frames,r.late
  return h.reason==nil,h.reason
end
return M
