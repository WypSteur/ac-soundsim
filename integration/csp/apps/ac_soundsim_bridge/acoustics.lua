-- Listener transfer, not a second engine model or a fabricated intake bus.
-- Indices: official FMOD wrapper; UNITS verified on AC's FMOD 1.08.12 DLL.
-- Unlike modern FMOD: Fader has no gain parameter; ParamEQ gain is dB.
-- CSP uses 0-based DSP indices. Native ordering/output needs live validation.
local M = {}
function M.defaults()
  return {enabled=true, trimDb=-2, midDb=-2, highDb=-10, highHz=2200,
    bodyHz=180, bodyDb=0, guard=true, minDistance=1, maxDistance=200,
    insideCone=120, outsideCone=240, outsideVolume=0.6, tailY=0.33, tailZ=-2.20}
end
function M.isCabin(car, sim, enums)
  if sim.focusedCar~=nil and sim.focusedCar~=0 then return false end
  -- Installed CSP explicitly describes this field as "inside this car".
  -- Do not classify every F5/F6/bonnet camera as interior.
  if car.isCameraOnBoard~=nil then return car.isCameraOnBoard==true end
  if not enums.CameraMode then return false end
  return sim.cameraMode==enums.CameraMode.Cockpit or
    (sim.cameraMode==enums.CameraMode.Drivable and enums.DrivableCamera and
      sim.driveableCameraMode==enums.DrivableCamera.Dash) or false
end
function M.advance(current, target, dt)
  if current==nil then return target end
  if type(dt)~='number' or dt~=dt then dt=0 end
  local nextMix=current+(target-current)*(1-math.exp(-math.max(0,math.min(0.1,dt))/0.08))
  if math.abs(nextMix-target)<0.000001 then return target end
  return nextMix
end
function M.chain(dsp, guard)
  -- Uniform three-band EQ gain: old Fader has NO gain parameter. Three gain
  -- stages keep each band below the verified +10 dB ceiling even at gain12.
  assert(dsp.ThreeEQ and dsp.ParamEQ, 'CSP cabin DSP unavailable')
  local chain={dsp.ThreeEQ,dsp.ParamEQ,dsp.ThreeEQ,dsp.ThreeEQ,dsp.ThreeEQ}
  if guard then assert(dsp.Limiter, 'CSP peak limiter unavailable'); chain[6]=dsp.Limiter end
  return chain
end
function M.apply(event, cache, cfg, gain, mix, spatial)
  local function param(dsp, key, value)
    local id=dsp..':'..key
    if cache[id]==nil or math.abs(cache[id]-value)>0.00001 or
        ((mix==0 or mix==1) and cache[id]~=value) then
      event:setDSPParameter(dsp,key,value); cache[id]=value
    end
  end
  -- Keep channel/camera gains at unity: gain precedes our optional limiter.
  -- No global/native FMOD gain is touched. No automatic loudness normalization.
  event.volume=1
  event.cameraInteriorMultiplier=1; event.cameraExteriorMultiplier=1; event.cameraTrackMultiplier=1
  param(0,0,0); param(0,1,cfg.midDb*mix); param(0,2,cfg.highDb*mix)
  param(0,3,400); param(0,4,cfg.highHz) -- keep native default crossover slope
  param(1,0,cfg.bodyHz); param(1,1,1.4)
  param(1,2,cfg.bodyDb*mix) -- AC's FMOD 1.08.12 ParamEQ GAIN is dB
  local db=20*math.log(gain)/math.log(10)+cfg.trimDb*mix
  for i=2,4 do
    for key=0,2 do param(i,key,db/3) end
    param(i,3,400); param(i,4,4000)
  end
  if cfg.guard then
    param(5,0,25); param(5,1,-1); param(5,2,0) -- mode left default; mono source
  end
  if spatial then
    if cache.distanceMin~=cfg.minDistance then event:setDistanceMin(cfg.minDistance); cache.distanceMin=cfg.minDistance end
    if cache.distanceMax~=cfg.maxDistance then event:setDistanceMax(cfg.maxDistance); cache.distanceMax=cfg.maxDistance end
    local cone=cfg.insideCone..':'..cfg.outsideCone..':'..cfg.outsideVolume
    if cache.cone~=cone then event:setConeSettings(cfg.insideCone,cfg.outsideCone,cfg.outsideVolume); cache.cone=cone end
  end
end
-- Read-only producer telemetry. This is NOT FMOD output or a consumer cursor.
M.pcmLayout=[[
  int32_t sampleRate; int32_t channels; int32_t fmodFormat;
  int32_t decodeSamples; int32_t decodeBytes; int32_t reserved20;
  volatile int64_t publishedBytes; int32_t reserved32[8]; float samples[112896];
]]
function M.measure(pcm)
  if pcm.sampleRate~=44100 or pcm.channels~=1 or pcm.fmodFormat~=5 or
      pcm.decodeSamples~=1764 or pcm.decodeBytes~=7056 then return nil,'unsupported PCM header' end
  local before=tonumber(pcm.publishedBytes)
  if before<=0 or before%4~=0 then return nil,'no complete PCM published' end
  local count=math.min(44100,before/4)
  local start=before/4-count
  local sum,peak,nearFull=0,0,0
  for i=0,count-1 do
    local v=tonumber(pcm.samples[(start+i)%112896])
    if v~=v or v==math.huge or v==-math.huge then return nil,'non-finite PCM' end
    local a=math.abs(v); sum=sum+v*v; peak=math.max(peak,a)
    if a>=32767/32768 then nearFull=nearFull+1 end
  end
  if before~=tonumber(pcm.publishedBytes) then return nil,'producer moved during measurement' end
  return {peak=peak,rms=math.sqrt(sum/count),nearFull=nearFull,frames=count}
end
jit.off(M.measure,true)
return M
