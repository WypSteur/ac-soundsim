"""LuaJIT ABI/logic check with a mocked CSP host, NOT an in-game audio test.
Pass bridge/acoustics sources as a JSON object on stdin.
Requires local dev-only lupa 2.6; no file IO or CSP native implementation here.
"""
import json
import sys
from lupa.luajit21 import LuaRuntime

lua = LuaRuntime(unpack_returned_tuples=True)
lua.execute(r'''
ffi=require('ffi')
script={}; logs={}; reports={}; mappings={}; backing={}; events={}; clock=0
nativeGains={[0]=.8,[1]=.6,[10]=.7,[11]=.9,[14]=.9,[15]=.6,[5]=.8,[20]=.7}; nativeWrites={}; clicked=nil; sliderValue=nil; sliderOverrides={}
expectedFX={}
for id,v in pairs(nativeGains) do if id~=0 and id~=1 then expectedFX[id]=v end end
for id=100,200 do nativeGains[id]=id/100; expectedFX[id]=id/100 end -- arbitrary mod events, no whitelist needed
function fxOriginal()
  for id,v in pairs(expectedFX) do if nativeGains[id]~=v then return false end end
  return true
end
function vec3(x,y,z) return {x=x or 0,y=y or 0,z=z or 0} end
car={rpm=700,gas=0,gear=0,clutch=0,turboBoost=0,isConnected=true,resetCounter=0,
  position=vec3(1,2,3),velocity=vec3(4,5,6),look=vec3(0,0,1),up=vec3(0,1,0),
  id=function() return 'ks_toyota_gt86' end,
  bodyTransform={transformPoint=function(self,p) return vec3(p.x+1,p.y+2,p.z+3) end}}
sim={time=1000,dt=1/60,isPaused=false,isReplayActive=false,cameraMode=2,driveableCameraMode=0,focusedCar=0}
os.preciseClock=function() return clock end
io.relative=function(p) return p end
io.save=function(p,s) reports[p]=s; return true end
ac={AudioDSP={Fader='fader',ThreeEQ='threeeq',ParamEQ='parameq',Limiter='limiter'},
  CameraMode={Cockpit=0,Drivable=2,Track=3,OnBoardFree=5,Free=6},DrivableCamera={Chase=0,Bonnet=2,Dash=4},
  FolderID={Logs=7},getFolder=function() return 'logs' end,getCar=function() return car end,getSim=function() return sim end,
  log=function(s) table.insert(logs,s) end,error=function(s) error(s) end,
  disposeMemoryMappedFile=function() end,onRelease=function(f) release=f end}
local function mapping(name,layout)
  local ct=ffi.typeof('struct {'..layout..'}')
  backing[name]=ffi.new(ffi.typeof('$[1]',ct))
  mappings[name]=ffi.cast(ffi.typeof('$*',ct),backing[name])
  return mappings[name]
end
ac.CarAudioEventID={EngineExt=0,EngineInt=1,BackfireExt=10,BackfireInt=11,Limiter=15}
ac.CarAudioTweak={getVolume=function(id) assert(id==0 or id==1,'complementary FMOD queried'); return nativeGains[id] end,
  setVolume=function(id,v) assert(id==0 or id==1,'complementary FMOD altered'); nativeGains[id]=v; table.insert(nativeWrites,{id,v}) end}
ac.writeMemoryMappedFile=function(name,layout) return mappings[name] or mapping(name,layout) end
ac.readMemoryMappedFile=function(name,layout) return mappings[name] or mapping(name,layout) end
ac.AudioEvent={fromFile=function(params,reverb)
  assert(params.useOcclusion==params.use3D and reverb==params.use3D)
  assert(params.dopplerEffect==(params.use3D and 1 or 0))
  assert(params.stream.size==451648)
  assert(params.dsp[1]=='threeeq' and params.dsp[2]=='parameq')
  assert(params.dsp[3]=='threeeq' and params.dsp[4]=='threeeq' and params.dsp[5]=='threeeq')
  assert(#params.dsp==5 or (#params.dsp==6 and params.dsp[6]=='limiter'))
  local e={spatial=params.use3D,chain=params.dsp,params={},writes=0,
    isValid=function(self) return not self.disposed end,isPlaying=function(self) return self.playing end,
    resume=function(self) assert(self.params['4:0']~=nil,'resumed before configuring gain'); self.playing=true end,
    dispose=function(self) self.disposed=true end,
    setDSPParameter=function(self,dsp,key,v)
      assert(dsp>=0 and dsp<#self.chain and v==v)
      self.params[dsp..':'..key]=v; self.writes=self.writes+1
    end,
    setDistanceMin=function(self,v) self.minDistance=v end,
    setDistanceMax=function(self,v) self.maxDistance=v end,
    setConeSettings=function(self,a,b,v) self.cone={a,b,v} end,
    setPosition=function(self,p,d,u,v) self.pos=p; self.dir=d; self.vel=v end,
    getDSPMetering=function() return .01,.02,.01,.02 end}
  table.insert(events,e); return e
end}
ui={text=function() end,separator=function() end,textWrapped=function() end,
  slider=function(label,v,lo,hi,format)
    if label=='SoundSim gain' then
      assert(lo==.1 and hi==12,'output gain range regression'); return sliderValue or v
    end
    assert(not label:find('FMOD') and lo<hi,'per-FMOD control regression')
    return sliderOverrides[label] or v
  end,
  button=function(label) if clicked==label then clicked=nil; return true end; return false end}
''')
payload = json.load(sys.stdin)
lua.globals().acoustics = lua.execute(payload['acoustics'])
lua.execute("package.loaded.acoustics=acoustics")
lua.globals().transportHealth = lua.execute(payload['health'])
lua.execute("package.loaded.transport_health=transportHealth")
lua.execute(payload['bridge'])
lua.execute(r'''
-- Mock published frame counters follow mock wall time unless a test stalls them.
autoPCM=true
local update=script.update
function script.update(dt)
  local r=mappings['AcTools.ACSoundSim.Status.v1']
  if autoPCM and r and r.commit%2==0 then r.frames=math.floor(clock*44100)+294 end
  update(dt)
end
''')
lua.execute(r'''
function ownGain(e)
  return e.volume*10^((e.params['2:0']+e.params['3:0']+e.params['4:0'])/20)
end
function approx(a,b) return math.abs(a-b)<0.000001 end
local health=transportHealth.new()
local probe={generation=1,audioName='test',resets=1,frames=100,late=0,modeID=1}
assert(transportHealth.check(health,probe,0))
probe.frames=200; assert(transportHealth.check(health,probe,.2))
assert(not transportHealth.check(health,probe,.501),'PCM stall not rejected')
probe.frames=300; assert(not transportHealth.check(health,probe,.6),'fault did not latch')
probe.generation=2; assert(transportHealth.check(health,probe,.7),'new producer not recovered')
probe.frames=1000; probe.late=20
assert(not transportHealth.check(health,probe,1.8),'sustained lost blocks not rejected')
probe.resets=2; assert(transportHealth.check(health,probe,2),'engine reset not recovered')
probe.modeID=2; assert(not transportHealth.check(health,probe,3))
probe.modeID=1; probe.frames=1200; assert(transportHealth.check(health,probe,4),'pause created false stall')
local cfg=acoustics.defaults()
assert(not acoustics.isCabin(car,sim,ac))
sim.cameraMode=0; assert(acoustics.isCabin(car,sim,ac))
sim.cameraMode=2; sim.driveableCameraMode=4; assert(acoustics.isCabin(car,sim,ac))
sim.driveableCameraMode=2; assert(not acoustics.isCabin(car,sim,ac),'bonnet treated as cabin')
sim.cameraMode=5; assert(not acoustics.isCabin(car,sim,ac),'all F5 views treated as cabin')
car.isCameraOnBoard=true; assert(acoustics.isCabin(car,sim,ac))
sim.focusedCar=1; assert(not acoustics.isCabin(car,sim,ac),'other car cabin colored player exhaust')
sim.focusedCar=0; car.isCameraOnBoard=false; sim.cameraMode=0
assert(not acoustics.isCabin(car,sim,ac),'explicit exterior flag ignored')
car.isCameraOnBoard=nil; sim.cameraMode=2; sim.driveableCameraMode=0
assert(acoustics.advance(nil,1,1/60)==1)
local fade=acoustics.advance(0,1,1/60); assert(fade>0 and fade<1)
assert(acoustics.advance(.5,1,0/0)==.5)
-- Producer meter: exact ABI, ring wrap, silence, near-full/NaN/header rejection.
local ct=ffi.typeof('struct {'..acoustics.pcmLayout..'}')
local pcm=ffi.new(ffi.typeof('$[1]',ct)); local pm=pcm[0]
assert(ffi.sizeof(pm)==451648 and ffi.offsetof(ct,'samples')==64)
pm.sampleRate=44100; pm.channels=1; pm.fmodFormat=5; pm.decodeSamples=1764; pm.decodeBytes=7056
assert(acoustics.measure(pm)==nil)
pm.publishedBytes=16; pm.samples[0]=.25; pm.samples[1]=-.5; pm.samples[2]=0; pm.samples[3]=1
local measured=acoustics.measure(pm)
assert(measured.peak==1 and measured.nearFull==1 and measured.frames==4)
assert(approx(measured.rms,math.sqrt(1.3125/4)))
pm.samples[0]=0; pm.samples[1]=0; pm.samples[3]=0; pm.samples[112895]=.75
pm.publishedBytes=(112896+2)*4
measured=acoustics.measure(pm); assert(measured.peak==.75 and measured.frames==44100,'ring wrap wrong')
pm.samples[112895]=0/0; assert(acoustics.measure(pm)==nil)
pm.samples[112895]=0; pm.channels=2; assert(acoustics.measure(pm)==nil)
pm.channels=1; pm.publishedBytes=5; assert(acoustics.measure(pm)==nil)
local unstable={sampleRate=44100,channels=1,fmodFormat=5,decodeSamples=1764,decodeBytes=7056,publishedBytes=16}
unstable.samples=setmetatable({}, {__index=function(_,i) if i==1 then unstable.publishedBytes=20 end; return .2 end})
local result,reason=acoustics.measure(unstable)
assert(result==nil and reason=='producer moved during measurement','torn PCM observation accepted')
script.update(1/60)
local s=mappings['AcTools.ACSoundSim.State.v1']
local r=mappings['AcTools.ACSoundSim.Status.v1']
assert(ffi.sizeof(s[0])==192 and ffi.sizeof(r[0])==368)
assert(ffi.offsetof(ffi.typeof(s[0]),'carID')==120)
assert(ffi.offsetof(ffi.typeof(r[0]),'audioName')==112)
assert(s.commit==2 and s.timestampSeconds==1 and s.rpm==700 and s.flags==3)
assert(ffi.string(s.carID)=='ks_toyota_gt86')
r.magic=0x53534143; r.version=1; r.size=368; r.generation=1; r.mode=1
r.audioSize=451648; r.sampleRate=44100; r.heartbeat=1; r.commit=2
ffi.copy(r.audioName,'AcTools.ACSoundSim.Audio.test\0')
clock=1.1; script.update(1/60)
assert(#events==1 and events[1].playing and not events[1].disposed)
assert(approx(events[1].pos.z,.8) and approx(events[1].pos.y,2.33) and events[1].dir.z==-1 and events[1].vel.x==4)
assert(approx(ownGain(events[1]),8) and events[1].cameraInteriorMultiplier==1,'requested doubled output gain regression')
assert(events[1].params['0:1']==0 and events[1].params['0:2']==0 and events[1].params['1:2']==0,'exterior EQ not neutral')
assert(events[1].params['5:1']==-1 and events[1].params['5:2']==0,'guard has makeup gain / wrong ceiling')
assert(nativeGains[0]==0 and nativeGains[1]==0,'native engine not muted')
assert(fxOriginal(),'complementary FMOD changed at default source gain')
assert(nativeGains[14]==.9 and nativeGains[5]==.8 and nativeGains[20]==.7,'transmission/wind/tyres changed')
windowMain()
sliderValue=3; windowMain(); script.update(1/60)
assert(approx(ownGain(events[1]),3) and #events==1,'live gain did not update existing event')
assert(fxOriginal(),'changing SoundSim gain affected FMOD')
sliderValue=4; windowMain(); script.update(1/60)
local beforeDouble=ownGain(events[1])
sliderValue=8; windowMain(); script.update(1/60)
assert(approx(ownGain(events[1]),2*beforeDouble) and #events==1 and fxOriginal(),'output doubling changed source lifecycle / FMOD')
sliderValue=100; windowMain(); script.update(1/60); assert(approx(ownGain(events[1]),12))
for i=2,4 do for key=0,2 do assert(events[1].params[i..':'..key]<=10,'FMOD band gain exceeds limit') end end
sliderValue=-1; windowMain(); script.update(1/60); assert(approx(ownGain(events[1]),.1))
sliderValue=0/0; windowMain(); script.update(1/60); assert(approx(ownGain(events[1]),.1),'NaN gain accepted')
sliderValue=2; windowMain(); script.update(1/60); sliderValue=nil
assert(fxOriginal(),'FX changed across source gain clamps')
r.commit=3; clock=1.15; script.update(1/60)
assert(#events==1 and not events[1].disposed,'torn status caused audio disconnect')
clock=1.5; script.update(1/60)
assert(events[1].disposed,'stale producer kept playing')
assert(nativeGains[0]==.8 and nativeGains[1]==.6,'stale producer did not restore native')
assert(fxOriginal(),'stale producer did not restore FX')
r.commit=4; r.heartbeat=2; clock=2.2; script.update(1/60)
assert(#events==2 and not events[2].disposed)
assert(nativeGains[0]==0 and nativeGains[1]==0)
sim.isPaused=true; sim.dt=0; clock=2.3; script.update(1/60)
assert(events[2].disposed and s.flags==7,'pause not propagated')
assert(nativeGains[0]==.8 and nativeGains[1]==.6,'pause did not restore native')
assert(fxOriginal(),'pause did not restore FX')
sim.isPaused=false; sim.dt=1/60; r.heartbeat=3; clock=3.4; script.update(1/60)
assert(nativeGains[0]==0 and nativeGains[1]==0)
clicked='Restore native engine'; windowMain()
script.update(1/60); assert(fxOriginal(),'native engine restore left FX attenuated')
assert(nativeGains[0]==.8 and nativeGains[1]==.6,'UI restore lost original gains')
clicked='Mute native engine for test'; windowMain(); script.update(1/60)
assert(nativeGains[0]==0 and nativeGains[1]==0)
clicked='Mute SoundSim test source'; windowMain(); script.update(1/60)
assert(nativeGains[0]==.8 and nativeGains[1]==.6,'muting mod did not restore native')
assert(fxOriginal(),'muting mod did not restore FX')
clicked='Enable SoundSim test source'; windowMain(); clock=4.5; r.heartbeat=4; script.update(1/60)
assert(nativeGains[0]==0 and nativeGains[1]==0)
-- Another script wins; restore only our still-owned zero and never fight it.
nativeGains[0]=.4; script.update(1/60)
assert(nativeGains[0]==.4 and nativeGains[1]==.6,'gain conflict overwrote other script')
assert(fxOriginal(),'engine ownership conflict left FX modified')
clicked='Mute native engine for test'; windowMain(); script.update(1/60)
assert(nativeGains[0]==0 and nativeGains[1]==0)
release(); assert(nativeGains[0]==.4 and nativeGains[1]==.6 and fxOriginal(),'release did not restore gains')
-- Return to native-enabled state before testing target loss.
clicked='Restore native engine'; windowMain()
clock=5.4; r.heartbeat=5; script.update(1/60)
local previous=events[#events]; clicked='Listen in 2D (audit)'; windowMain(); script.update(1/60)
assert(previous.disposed and not events[#events].spatial and not events[#events].pos,'2D audit not cleanly attached')
previous=events[#events]; clicked='Return to 3D CSP'; windowMain(); script.update(1/60)
assert(previous.disposed and events[#events].spatial and events[#events].pos,'3D restoration failed')
clicked='Mute native engine for test'; windowMain(); script.update(1/60)
assert(fxOriginal(),'FX changed after reacquiring continuous engine ownership')
-- Other mods can alter arbitrary/native FX while we play. We neither inspect
-- nor override them, even when output gain and diagnostic spatial mode change.
nativeGains[15]=.42; expectedFX[15]=.42
nativeGains[100]=.33; expectedFX[100]=.33
ac.CarAudioEventID.Limiter=nil; ac.CarAudioEventID.BackfireExt=nil
sliderValue=2.5; windowMain(); script.update(1/60)
assert(approx(ownGain(events[#events]),2.5) and fxOriginal(),'other mod gain overwritten')
sliderValue=nil
-- Live cabin changes must not recreate/restart the stream or modify native FX.
local e=events[#events]; local count=#events
sim.cameraMode=0
for i=1,90 do script.update(1/60) end
assert(#events==count and e.playing and not e.disposed)
assert(approx(e.params['0:2'],-10) and approx(e.params['0:1'],-2),'cabin EQ did not engage')
assert(approx(ownGain(e),2.5*10^(-2/20)))
sliderOverrides['Cabin body EQ dB']=3; windowMain(); script.update(1/60)
assert(approx(e.params['1:2'],3),'installed ParamEQ dB passed as linear gain')
sliderOverrides={}; clicked='Bypass cabin treatment (A/B)'; windowMain()
for i=1,90 do script.update(1/60) end
assert(#events==count and approx(ownGain(e),2.5) and approx(e.params['0:2'],0),'A/B bypass failed')
clicked='Enable cabin treatment'; windowMain(); sim.cameraMode=2
for i=1,90 do script.update(1/60) end
assert(approx(e.params['1:2'],0),'body resonance leaked into exterior')
local oldWrites=e.writes; script.update(1/60); assert(e.writes==oldWrites,'steady DSP parameters re-written each frame')
sliderOverrides['Distance minimum']=2; sliderOverrides['Cone outside volume']=.3
sliderOverrides['Exhaust longitudinal offset']=-2.4; windowMain(); script.update(1/60)
assert(#events==count and e.minDistance==2 and e.cone[3]==.3 and approx(e.pos.z,.6),'live spatial controls failed')
sliderOverrides={}; clicked='Disable peak guard (audit)'; windowMain(); script.update(1/60)
assert(e.disposed and #events==count+1 and #events[#events].chain==5,'guard bypass did not replace DSP chain')
clicked='Reset acoustic settings'; windowMain(); script.update(1/60)
assert(#events[#events].chain==6 and events[#events].minDistance==1,'default reset failed')
-- Real producer ring is read-only: verify integration opens it and logs real PCM.
local stream=mappings['AcTools.ACSoundSim.Audio.test']
assert(stream and ffi.sizeof(stream[0])==451648)
stream.sampleRate=44100; stream.channels=1; stream.fmodFormat=5
stream.decodeSamples=1764; stream.decodeBytes=7056; stream.publishedBytes=16
stream.samples[0]=.25; stream.samples[1]=-.5
clock=6.7; r.heartbeat=6; script.update(1/60)
assert(reports['logs/ac_soundsim_bridge.txt']:find('sourcePeak=0.50000'),'real PCM peak not logged')
assert(stream.publishedBytes==16 and stream.samples[1]==-.5,'producer ring was mutated')
assert(fxOriginal(),'cabin/guard/spatial controls affected complementary FMOD')
-- Native fallback for replay, engine faults, diagnostics and producer restart.
-- Continuous moving/rotating source, front/rear, near/far: no event recreation
-- and unchanged DSP/native FX. Mock proves poses/config, not native Doppler.
local flyby=events[#events]; local flybyCount=#events
sim.cameraPosition=vec3(0,0,0)
for i=1,120 do
  local angle=i*math.pi/60
  car.look=vec3(math.sin(angle),0,math.cos(angle)); car.velocity=vec3(0,0,-20)
  car.bodyTransform={transformPoint=function(self,p)
    return vec3(p.z*math.sin(angle),2+p.y,120-i*2+p.z*math.cos(angle))
  end}
  clock=clock+1/60; r.heartbeat=r.heartbeat+1; script.update(1/60)
  assert(#events==flybyCount and not flyby.disposed and flyby.playing)
  assert(approx(flyby.dir.x,-math.sin(angle)) and approx(flyby.dir.z,-math.cos(angle)) and flyby.vel.z==-20)
  assert(fxOriginal())
end
-- Genuine transport degradation must restore native, stay latched, then retry.
autoPCM=false; local stalled=events[#events]
clock=clock+.31; r.heartbeat=r.heartbeat+1; script.update(1/60)
assert(stalled.disposed and nativeGains[0]==.4 and nativeGains[1]==.6,'PCM stall did not restore native')
autoPCM=true; clock=clock+.1; r.heartbeat=r.heartbeat+1; script.update(1/60)
assert(events[#events].disposed,'stalled transport auto-reacquired')
clicked='Retry SoundSim after transport fault'; windowMain(); script.update(1/60)
assert(events[#events].playing and nativeGains[0]==0 and nativeGains[1]==0,'explicit health retry failed')
local overloaded=events[#events]
clock=clock+1.1; r.lateBlocks=r.lateBlocks+20; r.heartbeat=r.heartbeat+1; script.update(1/60)
assert(overloaded.disposed and nativeGains[0]==.4 and nativeGains[1]==.6 and fxOriginal(),'cadence degradation did not restore native')
r.generation=r.generation+1; r.lateBlocks=0; clock=clock+1.1; r.heartbeat=r.heartbeat+1; script.update(1/60)
assert(events[#events].playing and nativeGains[0]==0,'new generation did not recover health')
-- Restore monotonically increasing mock clock for the following legacy cases.
clock=math.max(clock,8); r.heartbeat=r.heartbeat+1; script.update(1/60)
sim.isReplayActive=true; script.update(1/60)
assert(events[#events].disposed and s.flags==11 and nativeGains[0]==.4 and nativeGains[1]==.6,'replay did not release replacement')
sim.isReplayActive=false; r.mode=6; r.heartbeat=7; clock=clock+1.1; script.update(1/60)
assert(nativeGains[0]==.4 and nativeGains[1]==.6,'fault did not preserve native fallback')
r.mode=1; r.flags=1; r.heartbeat=8; clock=clock+1.1; script.update(1/60)
assert(nativeGains[0]==.4 and nativeGains[1]==.6,'diagnostic tone muted real engine')
r.flags=2; script.update(1/60); assert(nativeGains[0]==0 and nativeGains[1]==0)
local previous=events[#events]
ffi.copy(r.audioName,'AcTools.ACSoundSim.Audio.restarted\0'); r.heartbeat=9; clock=clock+1.1; script.update(1/60)
assert(previous.disposed and events[#events].playing and fxOriginal(),'producer restart retained old event / modified FX')
-- Expected failure paths log, unlike the strict default test error handler.
ac.error=function(message) table.insert(logs,message) end
local originalGet,originalSet=ac.CarAudioTweak.getVolume,ac.CarAudioTweak.setVolume
local failRead=true
ac.CarAudioTweak.getVolume=function(id)
  if id==0 and failRead then failRead=false; error('transient native read failure') end
  return originalGet(id)
end
clicked='Mute SoundSim test source'; windowMain(); script.update(1/60)
assert(nativeGains[0]==0 and nativeGains[1]==.6,'restoration failure was not retained')
script.update(1/60)
assert(nativeGains[0]==.4 and nativeGains[1]==.6,'read failure discarded original gain ledger')
ac.CarAudioTweak.getVolume=originalGet
clicked='Enable SoundSim test source'; windowMain(); clock=clock+1.1; r.heartbeat=r.heartbeat+1; script.update(1/60)
local failRestore=true
ac.CarAudioTweak.setVolume=function(id,value)
  if id==0 and value~=0 and failRestore then failRestore=false; error('transient native write failure') end
  originalSet(id,value)
end
clicked='Mute SoundSim test source'; windowMain(); script.update(1/60)
assert(nativeGains[0]==0 and nativeGains[1]==.6,'failed restoration write was not retained')
script.update(1/60)
assert(nativeGains[0]==.4 and nativeGains[1]==.6,'write failure discarded original gain ledger')
-- A setter silently doing nothing is not successful restoration either.
ac.CarAudioTweak.setVolume=originalSet
clicked='Enable SoundSim test source'; windowMain(); clock=clock+1.1; r.heartbeat=r.heartbeat+1; script.update(1/60)
local dropRestore=true
ac.CarAudioTweak.setVolume=function(id,value)
  if id==0 and value~=0 and dropRestore then dropRestore=false; return end
  originalSet(id,value)
end
clicked='Mute SoundSim test source'; windowMain(); script.update(1/60)
assert(nativeGains[0]==0 and nativeGains[1]==.6,'unverified native restoration accepted')
script.update(1/60)
assert(nativeGains[0]==.4 and nativeGains[1]==.6,'native restoration readback did not trigger retry')
-- A partially applied mute must roll back both original values on exception.
local failMute=true
ac.CarAudioTweak.setVolume=function(id,value)
  if id==1 and value==0 and failMute then failMute=false; error('partial native mute failure') end
  originalSet(id,value)
end
clicked='Enable SoundSim test source'; windowMain(); clock=clock+1.1; r.heartbeat=r.heartbeat+1; script.update(1/60)
assert(events[#events].disposed and nativeGains[0]==.4 and nativeGains[1]==.6 and fxOriginal(),'partial mute did not roll back')
ac.CarAudioTweak.setVolume=originalSet
clock=clock+1.1; r.heartbeat=r.heartbeat+1; script.update(1/60)
assert(nativeGains[0]==0 and nativeGains[1]==0,'recovery after partial mute failed')
local transform=car.bodyTransform
car.bodyTransform={transformPoint=function() return vec3(0/0,0,0) end}
script.update(1/60)
assert(events[#events].disposed and nativeGains[0]==.4 and nativeGains[1]==.6 and fxOriginal(),'invalid transform kept replacement active')
car.bodyTransform=transform; clock=clock+1.1; r.heartbeat=r.heartbeat+1; script.update(1/60)
assert(events[#events].playing and nativeGains[0]==0 and nativeGains[1]==0,'emitter transform recovery failed')
sim.isPaused=false; sim.dt=1/60; car.id=function() return 'wrong_car' end
local beforeEvents=#events
r.heartbeat=10; clock=clock+.1; script.update(1/60)
assert(#events==beforeEvents,'wrong car attached audio')
car=nil; clock=clock+.4; script.update(1/60)
assert(s.flags==2,'despawn still active')
release(); assert(s.flags==0)
assert(reports['logs/ac_soundsim_bridge.txt']:find('%[ACSoundSim%]'))
assert(fxOriginal(),'FX changed on target loss / release')
assert(reports['logs/ac_soundsim_bridge.txt']:find('FMOD_FX=untouched'),'output-only policy missing from report')
''')
print('PASS LuaJIT FFI bridge: cabin/DSP/guard, moving 3D poses, read-only PCM meter, transport latch/retry, native fallback/transient API errors, arbitrary FMOD unchanged (NOT native audio qualification)')
