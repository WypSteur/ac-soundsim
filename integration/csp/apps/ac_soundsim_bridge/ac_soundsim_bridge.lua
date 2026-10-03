local ffi = require('ffi')
local acoustics = require('acoustics')
local transportHealth = require('transport_health')
local TAG, target = '[ACSoundSim]', 'ks_toyota_gt86'
local stateName, statusName = 'AcTools.ACSoundSim.State.v1', 'AcTools.ACSoundSim.Status.v1'
local MAGIC = 0x53534143
local stateLayout = [[
  volatile uint32_t commit; uint32_t magic; uint32_t version; uint32_t size;
  uint32_t generation; uint32_t flags; uint32_t resetCounter; uint32_t carIndex;
  double timestampSeconds; double writerClock;
  float rpm; float throttle; float clutch; float boost;
  int32_t gear; uint32_t reserved;
  float position[3]; float velocity[3]; float look[3]; float up[3];
  char carID[64]; uint32_t padding[2];
]]
local statusLayout = [[
  volatile uint32_t commit; uint32_t magic; uint32_t version; uint32_t size;
  uint32_t generation; uint32_t mode; uint32_t stateCommit; uint32_t flags;
  double stateAgeSeconds; double requestedRpm; double effectiveRpm; double phase;
  uint64_t frames; uint64_t lateBlocks; uint64_t tornStates; uint64_t droppedStates; uint64_t resets;
  uint32_t audioSize; uint32_t sampleRate; char audioName[96];
  uint64_t heartbeat; double renderMs; double maxRenderMs;
  uint32_t faults; uint32_t reserved; char error[128];
]]
local modeNames = {[0]='waiting', 'running', 'paused', 'stale', 'wrong-car', 'invalid-state', 'engine-fault', 'replay-muted'}
local generation = math.random(1,1073741823)
local state, backend, event, eventName
local sequence, lastLog, nextOpen, nextAudio = 0,-1,0,0
local lastHeartbeat, heartbeatAt = '',-1
local lastGoodBackend
local health=transportHealth.new()
local healthState='waiting'
local enabled = true
-- Listening gain only: leave upstream PCM/combustion and AC's global mix alone.
local outputGain = 8.0 -- user was already at 4x: double that output, FMOD untouched
local spatial = true -- bypass is a listening diagnostic, production remains 3D
local acoustic = acoustics.defaults()
local acousticCache, cabinMix, cabinTarget = {},nil,false
local sourcePCM, sourceMeter, sourceMeterError
local cameraOverride = 'auto'
-- Session-only listening test. Never changes banks, car config or global volume.
local nativeTest, nativeSaved = true,nil
local nativeRestorePending=false
local nativeState = 'ON (waiting for SoundSim)'
local info = {carID='<none>',rpm=0,gas=0,gear=0,isTarget=false,error='',valid=false,playing=false}
local meter, emitter = 'not measured','not positioned'
local runtime = {mode='not connected',frames=0,late=0,age=-1,requested=0,effective=0,phase=0,renderMs=0,faults=0}
local function log(s) ac.log(TAG..' '..s) end
local function report(s)
  s=tostring(s)
  if info.error~=s then info.error=s; ac.error(TAG..' '..s) end
end
local function finite(v) return type(v)=='number' and v==v and v~=math.huge and v~=-math.huge end
local function vectorOK(v) return v and finite(v.x) and finite(v.y) and finite(v.z) end
local function restoreNative()
  if not nativeSaved then nativeRestorePending=false; return true end
  local car=ac.getCar(0)
  local pending={}
  local api=ac.CarAudioTweak or {}
  if car and car:id()==target then
    for _,s in ipairs(nativeSaved) do
      -- Do not overwrite another script/user change made while we owned zero.
      local ok,value=pcall(api.getVolume,s.id)
      if not ok or not finite(value) then pending[#pending+1]=s
      elseif value==0 then
        local restored=pcall(api.setVolume,s.id,s.value)
        local verified,current=pcall(api.getVolume,s.id)
        if not restored or not verified or not finite(current) or (current==0 and s.value~=0) then
          pending[#pending+1]=s
        end
      end
    end
  end
  nativeRestorePending=#pending>0
  nativeSaved=nativeRestorePending and pending or nil
  nativeState=nativeRestorePending and 'RESTORE PENDING (native API error; retrying)' or 'ON (restored / released)'
  return not nativeRestorePending
end
local function updateNative(car,sim,r)
  -- Keep original gains through a transient restoration API error. Finish
  -- releasing ownership before considering a new mute; never lose the ledger.
  if nativeRestorePending then restoreNative(); return end
  local wanted=nativeTest and enabled and car and car.isConnected and car:id()==target
    and r and r.modeID==1 and not r.diagnosticTone and info.valid and info.playing
    and not sim.isPaused and not sim.isReplayActive and sim.dt~=0
  if not wanted then
    if not restoreNative() then return end
    nativeState=nativeTest and 'ON (waiting for SoundSim)' or 'ON (test disabled)'
    return
  end
  local api,ids=ac.CarAudioTweak,ac.CarAudioEventID
  if not api or not ids or not api.getVolume or not api.setVolume then
    nativeTest=false; nativeState='ON (mute API unavailable)'; return
  end
  if not nativeSaved then
    local ext,int=api.getVolume(ids.EngineExt),api.getVolume(ids.EngineInt)
    if not finite(ext) or not finite(int) or ext<0 or int<0 then
      nativeTest=false; nativeState='ON (native gain unavailable)'; return
    end
    nativeSaved={{id=ids.EngineExt,value=ext},{id=ids.EngineInt,value=int}}
    -- Saved before either write, so partial failures are recoverable.
    api.setVolume(ids.EngineExt,0); api.setVolume(ids.EngineInt,0)
    log(string.format('native engine test muted; saved ext=%.4f int=%.4f',ext,int))
  end
  if api.getVolume(ids.EngineExt)~=0 or api.getVolume(ids.EngineInt)~=0 then
    nativeTest=false
    if not restoreNative() then return end
    nativeState='ON (gain conflict; test disabled)'
    return
  end
  nativeState='MUTED EngineExt / EngineInt (gain=0 / 0)'
end
local function disposeAudio()
  if event then event:dispose(); event=nil; eventName=nil end
  if sourcePCM then ac.disposeMemoryMappedFile(sourcePCM); sourcePCM=nil end
  acousticCache,cabinMix,sourceMeter,sourceMeterError={},nil,nil,nil
  info.valid,info.playing=false,false
end
local function vector(dst,v) dst[0],dst[1],dst[2]=v.x,v.y,v.z end
local function publish(car,sim,now)
  state.commit=sequence+1
  state.magic,state.version,state.size=MAGIC,1,192
  state.generation,state.writerClock=generation,now
  state.timestampSeconds=(sim.time or 0)/1000 -- SDK time is milliseconds
  state.carIndex=0
  state.flags=2+((sim.isPaused or sim.dt==0) and 4 or 0)+(sim.isReplayActive and 8 or 0)
  state.resetCounter=car and car.resetCounter or 0
  if car then
    info.carID,info.rpm,info.gas,info.gear=car:id(),car.rpm,car.gas,car.gear
    info.isTarget=info.carID==target
    if car.isConnected then state.flags=state.flags+1 end
    state.rpm,state.throttle,state.clutch,state.boost,state.gear=car.rpm,car.gas,car.clutch,car.turboBoost or 0,car.gear
    vector(state.position,car.position); vector(state.velocity,car.velocity)
    vector(state.look,car.look); vector(state.up,car.up)
    local id=info.carID:sub(1,63)
    ffi.copy(state.carID,id..string.rep('\0',64-#id),64)
  else
    info.carID,info.isTarget='<no car 0>',false
    state.rpm,state.throttle,state.clutch,state.boost,state.gear=0,0,0,0,0
    ffi.copy(state.carID,string.rep('\0',64),64)
  end
  sequence=(sequence+2)%4294967296
  if sequence==0 then sequence=2 end
  state.commit=sequence
end
-- Restricted to Windows x64: volatile commits, ordered interpreter writes, x86
-- store ordering. No JIT optimization across the seqlock publication boundaries.
jit.off(publish,true)
local function readBackend(now)
  if not backend and now>=nextOpen then
    nextOpen=now+1
    local ok,result=pcall(ac.readMemoryMappedFile,statusName,statusLayout)
    if ok then backend=result; assert(ffi.sizeof(backend[0])==368,'status ABI mismatch'); log('status MMF opened; schema=1 size=368 read-only') end
  end
  if not backend then return nil end
  local before=tonumber(backend.commit)
  if before==0 or before%2~=0 then return nil end
  local r={
    magic=tonumber(backend.magic),version=tonumber(backend.version),size=tonumber(backend.size),
    generation=tonumber(backend.generation),heartbeat=tonumber(backend.heartbeat),mode=tonumber(backend.mode),
    diagnosticTone=tonumber(backend.flags)==1,
    referenceFa20=tonumber(backend.flags)==2,
    audioSize=tonumber(backend.audioSize),sampleRate=tonumber(backend.sampleRate),
    audioName=ffi.string(backend.audioName,96):match('^[^%z]*'),
    age=tonumber(backend.stateAgeSeconds),requested=tonumber(backend.requestedRpm),effective=tonumber(backend.effectiveRpm),
    phase=tonumber(backend.phase),frames=tonumber(backend.frames),late=tonumber(backend.lateBlocks),
    renderMs=tonumber(backend.renderMs),faults=tonumber(backend.faults),resets=tonumber(backend.resets),error=ffi.string(backend.error,128):match('^[^%z]*')
  }
  if before~=tonumber(backend.commit) then return nil end
  if r.magic~=MAGIC or r.version~=1 or r.size~=368 then report('unsupported runtime status ABI'); return nil end
  if not finite(r.frames) or not finite(r.late) or r.frames<0 or r.late<0 then report('invalid producer counters'); return nil end
  local key=tostring(r.generation)..':'..tostring(r.heartbeat)
  if key~=lastHeartbeat then lastHeartbeat,heartbeatAt=key,now end
  if now-heartbeatAt>0.3 then runtime.mode='producer stale'; return nil end
  r.modeID=r.mode; r.mode=modeNames[r.mode] or 'unknown'; runtime=r
  return r
end
jit.off(readBackend,true)
local function readBackendSafe(now)
  local r=readBackend(now)
  if r then lastGoodBackend=r end
  -- An in-flight status write is not a disconnect: hold the last complete
  -- snapshot for at most 300 ms, without refreshing the producer heartbeat.
  if not r and now-heartbeatAt<=0.3 then return lastGoodBackend end
  return r
end
local function updateAudio(car,sim,r,now,dt)
  local healthy,reason=false,'waiting'
  if r then healthy,reason=transportHealth.check(health,r,now) end
  healthState=healthy and 'healthy' or (reason or 'waiting')
  local available=r and r.modeID==1 and info.isTarget and enabled and not sim.isPaused and not sim.isReplayActive and sim.dt~=0
    and healthy
  if not available then disposeAudio(); return end
  if r.audioSize~=451648 or r.sampleRate~=44100 or not r.audioName:match('^AcTools%.ACSoundSim%.Audio%.') then
    disposeAudio(); report('unsupported audio stream contract'); return
  end
  if eventName~=r.audioName then disposeAudio() end
  if not event and now>=nextAudio then
    nextAudio=now+1
    event=ac.AudioEvent.fromFile({stream={name=r.audioName,size=r.audioSize},use3D=spatial,useOcclusion=spatial,
      minDistance=1,maxDistance=200,insideConeAngle=120,outsideConeAngle=240,outsideVolume=0.6,dopplerEffect=spatial and 1 or 0,
      dsp=acoustics.chain(ac.AudioDSP,acoustic.guard)},spatial)
    eventName=r.audioName
    if not event:isValid() then disposeAudio(); report('CSP AudioEvent invalid'); return end
    log('audio attached: '..eventName..' size='..r.audioSize..' mono float32 44100 Hz spatial='..tostring(spatial))
  end
  if event then
    cabinTarget=cameraOverride=='interior' or (cameraOverride=='auto' and acoustics.isCabin(car,sim,ac))
    cabinMix=acoustics.advance(cabinMix,(spatial and acoustic.enabled and cabinTarget) and 1 or 0,dt)
    acoustics.apply(event,acousticCache,acoustic,outputGain,cabinMix,spatial)
    -- Midpoint of CSP's GT86 tailpipes (+/-0.455, 0.33, -2.20).
    -- One mono exhaust system, not duplicated coherent left/right sources.
    -- Lua app setPosition() uses WORLD coordinates.
    local pos=car.bodyTransform:transformPoint(vec3(0,acoustic.tailY,acoustic.tailZ))
    local dir=vec3(-car.look.x,-car.look.y,-car.look.z)
    if not vectorOK(pos) or not vectorOK(dir) or not vectorOK(car.velocity) or not vectorOK(car.up) then
      disposeAudio(); report('non-finite emitter transform'); return
    end
    if spatial then event:setPosition(pos,dir,car.up,car.velocity) end
    if not event:isPlaying() then event:resume() end -- configured before playback
    local cam=sim.cameraPosition
    local distance=cam and math.sqrt((pos.x-cam.x)^2+(pos.y-cam.y)^2+(pos.z-cam.z)^2) or -1
    emitter=string.format('world=(%.3f,%.3f,%.3f) velocity=(%.3f,%.3f,%.3f) cameraDistance=%.3f',pos.x,pos.y,pos.z,car.velocity.x,car.velocity.y,car.velocity.z,distance)
    info.valid,info.playing=event:isValid(),event:isPlaying()
    if not info.valid then disposeAudio(); report('CSP event became invalid') end
  end
end
local function measureSource()
  if not eventName then return end
  if not sourcePCM then
    local ok,result=pcall(ac.readMemoryMappedFile,eventName,acoustics.pcmLayout)
    if not ok then sourceMeterError='PCM meter unavailable: '..tostring(result); return end
    sourcePCM=result
    assert(ffi.sizeof(sourcePCM[0])==451648,'PCM meter ABI mismatch')
  end
  local result,err=acoustics.measure(sourcePCM)
  if result then sourceMeter=result; sourceMeterError=nil else sourceMeter=nil; sourceMeterError=err end
end
function script.update(dt)
  local now=os.preciseClock()
  local ok,err=pcall(function()
    if not state then
      state=ac.writeMemoryMappedFile(stateName,stateLayout)
      assert(ffi.sizeof(state[0])==192,'state ABI mismatch')
      sequence=tonumber(state.commit); if sequence%2~=0 then sequence=sequence+1 end
      log('state MMF created/opened; schema=1 size=192 generation='..generation)
    end
    local sim,car=ac.getSim(),ac.getCar(0)
    local r=readBackendSafe(now)
    publish(car,sim,now); updateAudio(car,sim,r,now,dt); updateNative(car,sim,r)
  end)
  if not ok then
    disposeAudio(); restoreNative(); report(err)
    -- Fail closed if a packet update was interrupted.
    if state then state.flags=0; sequence=(sequence+2)%4294967296; if sequence==0 then sequence=2 end; state.commit=sequence end
  end
  if lastLog<0 or now-lastLog>=1 then
    lastLog=now
    if event then
      local ok,a,b,c,d=pcall(function() return event:getDSPMetering(acoustic.guard and 5 or 4,'both') end)
      if ok then meter=string.format('%s, %s, %s, %s',tostring(a),tostring(b),tostring(c),tostring(d)) else meter='unavailable: '..tostring(a) end
      local measured,err=pcall(measureSource)
      if not measured then sourceMeter=nil; sourceMeterError=tostring(err) end
    else meter='no active event' end
    local line=string.format('car=%s target=%s rpm=%.0f gas=%.3f gear=%s seq=%s runtime=%s frames=%s late=%s eventValid=%s playing=%s',
      info.carID,tostring(info.isTarget),info.rpm,info.gas,tostring(info.gear),tostring(sequence),runtime.mode,
      tostring(runtime.frames),tostring(runtime.late),tostring(info.valid),tostring(info.playing))
    line=line..' faults='..tostring(runtime.faults or 0)
    line=line..string.format(' gain=%.2f spatial=%s cabin=%s cabinMix=%.3f trimDb=%.2f highDb=%.2f highHz=%.0f bodyDb=%.2f guard=%s native=%s',
      outputGain,tostring(spatial),tostring(cabinTarget),cabinMix or 0,acoustic.trimDb,acoustic.highDb,acoustic.highHz,acoustic.bodyDb,tostring(acoustic.guard),nativeState)
    line=line..string.format(' cameraMode=%s driveableMode=%s onBoard=%s override=%s midDb=%.2f bodyHz=%.0f distance=%.2f/%.0f cone=%.0f/%.0f/%.2f',
      tostring(ac.getSim().cameraMode),tostring(ac.getSim().driveableCameraMode),tostring(ac.getCar(0) and ac.getCar(0).isCameraOnBoard),
      cameraOverride,acoustic.midDb,acoustic.bodyHz,acoustic.minDistance,acoustic.maxDistance,acoustic.insideCone,acoustic.outsideCone,acoustic.outsideVolume)
    if sourceMeter then
      line=line..string.format(' sourcePeak=%.5f sourceRms=%.5f sourceNearFull=%d gainOnlyPeak=%.5f',sourceMeter.peak,sourceMeter.rms,sourceMeter.nearFull,sourceMeter.peak*outputGain)
    else line=line..' sourceMeter='..tostring(sourceMeterError or 'unavailable') end
    line=line..' FMOD_FX=untouched output_trim_only=false own_listener_DSP=true'
    line=line..' bridgeVersion=0.0.11 transportHealth='..healthState
    log(line..' DSP='..meter..' '..emitter)
    -- AC's ac.log is visible in the debug app but not necessarily its text log.
    -- Own report keeps the latest evidence without changing global log settings.
    local ok,saved=pcall(io.save,ac.getFolder(ac.FolderID.Logs)..'/ac_soundsim_bridge.txt',TAG..' '..line..'\nDSP raw metering='..meter..'\n'..emitter..'\nlastError='..info.error..'\n')
    if not ok or saved==false then report('report save failed: '..tostring(saved)) end
  end
end
function windowMain()
  ui.text('AC SoundSim Bridge 0.0.11 - M5 qualification'); ui.separator()
  ui.text('Target: '..target); ui.text('Detected: '..info.carID); ui.text('GT86 target match: '..tostring(info.isTarget))
  ui.text(string.format('RPM: %.0f   Throttle: %.3f   Gear: %s',info.rpm,info.gas,tostring(info.gear)))
  ui.separator(); ui.text('Runtime: '..runtime.mode)
  if runtime.diagnosticTone then ui.text('DIAGNOSTIC 440 Hz tone, NOT Engine-Sim') end
  if not runtime.diagnosticTone then
    ui.text(runtime.referenceFa20 and 'Source: FA20D reference port + public Engine-Sim DSP/IR' or 'Source: provisional M1 model (legacy)')
  end
  ui.text(string.format('AC requested / crank: %.0f / %.0f RPM',runtime.requested,runtime.effective))
  ui.text(string.format('State age: %.1f ms | render: %.2f ms',runtime.age*1000,runtime.renderMs))
  ui.text('PCM frames: '..tostring(runtime.frames)..' | producer late blocks: '..tostring(runtime.late))
  ui.text('Engine faults: '..tostring(runtime.faults or 0))
  ui.text('CSP event valid / playing: '..tostring(info.valid)..' / '..tostring(info.playing))
  ui.textWrapped('Producer health: '..healthState)
  if health.reason and ui.button('Retry SoundSim after transport fault') then
    health=transportHealth.new(); nextAudio=0
  end
  ui.text('DSP raw meter (not guaranteed): '..meter)
  if sourceMeter then
    ui.text(string.format('Producer PCM: peak %.3f / RMS %.3f | near full scale %d',sourceMeter.peak,sourceMeter.rms,sourceMeter.nearFull))
    ui.text(string.format('Peak x requested gain, BEFORE filters / 3D: %.3f',sourceMeter.peak*outputGain))
  else ui.textWrapped('Producer meter: '..tostring(sourceMeterError or 'waiting')) end
  ui.text(spatial and '3D CSP + listener DSP' or 'AUDIT 2D: cabin / spatial effects disabled; peak guard separate')
  if ui.button(spatial and 'Listen in 2D (audit)' or 'Return to 3D CSP') then
    spatial=not spatial; disposeAudio(); nextAudio=0
  end
  local selected=ui.slider('SoundSim gain',outputGain,0.1,12.0,'%.2fx')
  if finite(selected) then outputGain=math.max(0.1,math.min(12.0,selected)) end
  ui.textWrapped('Listening level, not calibrated. Lower gain if distorted; global audio unchanged.')
  if outputGain>2.85 then
    ui.textWrapped('High gain: peaks can exceed full scale. Guard may reduce dynamics; without it, lower gain if distorted. Global mix headroom is not guaranteed.')
  end
  if ui.button(acoustic.guard and 'Disable peak guard (audit)' or 'Enable peak guard') then
    acoustic.guard=not acoustic.guard; disposeAudio(); nextAudio=0
  end
  ui.textWrapped(acoustic.guard and 'Own peak guard ON: -1 dBFS ceiling, no makeup gain. Native output/order still needs validation.' or 'Own peak guard OFF: unprotected reference listening.')
  ui.separator(); ui.text('Cabin transfer: '..(cabinTarget and 'INTERIOR' or 'EXTERIOR')..' | override: '..cameraOverride)
  if ui.button(acoustic.enabled and 'Bypass cabin treatment (A/B)' or 'Enable cabin treatment') then acoustic.enabled=not acoustic.enabled end
  if ui.button('Cycle camera override (auto/interior/exterior)') then
    cameraOverride=cameraOverride=='auto' and 'interior' or (cameraOverride=='interior' and 'exterior' or 'auto')
  end
  local function setting(label,key,lo,hi,format)
    local value=ui.slider(label,acoustic[key],lo,hi,format)
    if finite(value) then acoustic[key]=math.max(lo,math.min(hi,value)) end
  end
  setting('Cabin level dB','trimDb',-12,0,'%.1f dB')
  setting('Cabin mids dB','midDb',-12,0,'%.1f dB')
  setting('Cabin highs dB','highDb',-24,0,'%.1f dB')
  setting('Cabin high crossover','highHz',1000,8000,'%.0f Hz')
  setting('Cabin body frequency','bodyHz',80,400,'%.0f Hz')
  setting('Cabin body EQ dB','bodyDb',-6,3,'%.1f dB')
  ui.textWrapped('Initial listening preset, NOT a measured GT86 cabin. Body EQ defaults neutral. Exterior cabin EQ is neutral. No fake intake/mechanical bus.')
  ui.separator(); ui.text('Exterior propagation (CSP)')
  setting('Exhaust height','tailY',0.1,0.8,'%.2f m')
  setting('Exhaust longitudinal offset','tailZ',-2.5,-1.5,'%.2f m')
  setting('Distance minimum','minDistance',0.5,3,'%.2f m')
  setting('Distance maximum','maxDistance',20,300,'%.0f m')
  setting('Cone inside','insideCone',60,180,'%.0f deg')
  setting('Cone outside','outsideCone',180,360,'%.0f deg')
  setting('Cone outside volume','outsideVolume',0.2,1,'%.2fx')
  if ui.button('Reset acoustic settings') then
    local oldGuard=acoustic.guard; acoustic=acoustics.defaults(); cameraOverride='auto'
    if oldGuard~=acoustic.guard then disposeAudio(); nextAudio=0 end
  end
  ui.textWrapped('Session-only settings. Doppler=1, source velocity from AC. Replays still muted. All complementary FMOD levels untouched.')
  if ui.button(enabled and 'Mute SoundSim test source' or 'Enable SoundSim test source') then enabled=not enabled end
  ui.textWrapped('Native engine: '..nativeState)
  if ui.button(nativeTest and 'Restore native engine' or 'Mute native engine for test') then
    nativeTest=not nativeTest
    if not nativeTest then restoreNative() end
  end
  ui.textWrapped('Only continuous native EngineInt/EngineExt replaced. Limiter, backfire, turbo/flutter, transmission, tyres and mod FX untouched. Session-only settings.')
  if info.error~='' then ui.textWrapped('Last error: '..info.error) end
end
ac.onRelease(function()
  restoreNative()
  disposeAudio()
  if state then state.commit=sequence+1; state.flags=0; state.commit=sequence+2; ac.disposeMemoryMappedFile(state); state=nil end
  if backend then ac.disposeMemoryMappedFile(backend); backend=nil end
end)
log('bridge 0.0.11 loaded; target='..target..'; M5 producer health guard; output gain=8; cabin/peak guard unchanged; complementary FMOD untouched')
