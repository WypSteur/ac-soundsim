-- AC SoundSim PCM/3D feasibility probe v0.2
-- Writes a local runtime report so diagnosis does not depend on finding CSP logs manually.

local probe = {
  runtimeStreamSymbols = {},
  sdkReport = '',
  sdkStreamFound = false,
  runtimeStreamFound = false,
  referenceEvent = nil,
  referenceStatus = 'NOT TESTED',
  lastError = '',
  appLoaded = true,
}

local runtimeReportPath = __dirname .. '/probe_runtime_report.txt'

local function lower(s)
  return string.lower(tostring(s or ''))
end

local function saveRuntimeReport(extra)
  local lines = {}
  lines[#lines + 1] = 'AC SoundSim PCM Probe v0.2'
  lines[#lines + 1] = 'APP_LOADED=' .. tostring(probe.appLoaded)
  lines[#lines + 1] = 'RUNTIME_STREAM_FOUND=' .. tostring(probe.runtimeStreamFound)
  lines[#lines + 1] = 'SDK_STREAM_FOUND=' .. tostring(probe.sdkStreamFound)
  lines[#lines + 1] = 'REFERENCE_3D_AUDIO=' .. tostring(probe.referenceStatus)
  lines[#lines + 1] = ''
  lines[#lines + 1] = 'Runtime stream/PCM symbols:'
  if #probe.runtimeStreamSymbols == 0 then
    lines[#lines + 1] = '  (none)'
  else
    for _, s in ipairs(probe.runtimeStreamSymbols) do
      lines[#lines + 1] = '  ' .. s
    end
  end
  lines[#lines + 1] = ''
  lines[#lines + 1] = 'LAST_ERROR:'
  lines[#lines + 1] = probe.lastError ~= '' and probe.lastError or '(none)'
  if extra then
    lines[#lines + 1] = ''
    lines[#lines + 1] = 'EXTRA:'
    lines[#lines + 1] = tostring(extra)
  end
  pcall(function() io.save(runtimeReportPath, table.concat(lines, '\n')) end)
end

local function addSymbol(name)
  for _, v in ipairs(probe.runtimeStreamSymbols) do
    if v == name then return end
  end
  probe.runtimeStreamSymbols[#probe.runtimeStreamSymbols + 1] = name
end

local function scanTable(tbl, prefix)
  if type(tbl) ~= 'table' then return end
  for k, v in pairs(tbl) do
    local name = prefix .. tostring(k)
    local l = lower(name)
    if l:find('stream', 1, true) or l:find('pcm', 1, true) then
      addSymbol(name .. ' [' .. type(v) .. ']')
    end
  end
end

local function scanRuntimeAPI()
  probe.runtimeStreamSymbols = {}
  scanTable(ac.AudioEvent, 'ac.AudioEvent.')
  local mt = getmetatable(ac.AudioEvent)
  if type(mt) == 'table' then
    scanTable(mt, 'getmetatable(ac.AudioEvent).')
    if type(mt.__index) == 'table' then
      scanTable(mt.__index, 'ac.AudioEvent.__index.')
    end
  end
  probe.runtimeStreamFound = #probe.runtimeStreamSymbols > 0
end

local function readSdkReport()
  local data = ''
  pcall(function()
    data = io.load(__dirname .. '/csp_stream_api_report.txt') or ''
  end)
  probe.sdkReport = data
  local l = lower(data)
  probe.sdkStreamFound =
    l:find('result_a_stream_api_found', 1, true) ~= nil or
    l:find('streaming audio', 1, true) ~= nil or
    l:find('audio stream', 1, true) ~= nil or
    l:find('audiostream', 1, true) ~= nil or
    l:find('fromstream', 1, true) ~= nil or
    (l:find('audioevent', 1, true) ~= nil and l:find('stream', 1, true) ~= nil)
end

local function disposeReference()
  if probe.referenceEvent then
    pcall(function() probe.referenceEvent:dispose() end)
    probe.referenceEvent = nil
  end
end

local function tryCreateReference(params, label, errors)
  local ok, result = pcall(function()
    return ac.AudioEvent.fromFile(params, true)
  end)
  if ok and result then
    return result, label
  end
  errors[#errors + 1] = label .. ': ' .. tostring(result)
  return nil, nil
end

local function createReferenceEvent()
  disposeReference()
  probe.lastError = ''
  local path = __dirname .. '/reference_3d.wav'
  local errors = {}
  local evt, used = nil, nil

  -- Try the most common parameter shapes because generated CSP definitions can vary by build.
  local attempts = {
    { { filename = path, loop = true }, 'filename+loop' },
    { { file = path, loop = true }, 'file+loop' },
    { { path = path, loop = true }, 'path+loop' },
    { { source = path, loop = true }, 'source+loop' },
    { { filename = path }, 'filename' },
    { { file = path }, 'file' },
    { { path = path }, 'path' },
  }

  for _, a in ipairs(attempts) do
    evt, used = tryCreateReference(a[1], a[2], errors)
    if evt then break end
  end

  if not evt then
    probe.referenceStatus = 'FAIL_CREATE_EVENT'
    probe.lastError = table.concat(errors, '\n')
    ac.error('[AC SoundSim Probe] Failed to create file-backed AudioEvent')
    saveRuntimeReport('Failed before playback.')
    return
  end

  local car = ac.getCar(0)
  local pos = car and car.position or vec3(0, 0, 0)
  local vel = car and car.velocity or vec3(0, 0, 0)

  local ok, err = pcall(function()
    evt.volume = 0.75
    evt.cameraInteriorMultiplier = 0.6
    evt.cameraExteriorMultiplier = 1.0
    evt.cameraTrackMultiplier = 1.0
    evt:setDistanceMin(1.0)
    evt:setDistanceMax(200.0)
    evt:setConeSettings(360, 360, 1.0)
    evt:setPosition(pos, nil, nil, vel)
    evt:start()
  end)

  if not ok then
    pcall(function() evt:dispose() end)
    probe.referenceStatus = 'FAIL_START_EVENT'
    probe.lastError = tostring(err)
    ac.error('[AC SoundSim Probe] AudioEvent created but failed to start')
    saveRuntimeReport('Creation mode: ' .. tostring(used))
    return
  end

  probe.referenceEvent = evt

  -- Give CSP a moment; validity/playing is also shown live in UI.
  local valid = false
  local playing = false
  pcall(function() valid = evt:isValid() end)
  pcall(function() playing = evt:isPlaying() end)

  probe.referenceStatus = valid and 'STARTED_VALID' or 'STARTED_BUT_INVALID'
  ac.log('[AC SoundSim Probe] Reference event created with mode: ' .. tostring(used))
  ac.log('[AC SoundSim Probe] isValid=' .. tostring(valid) .. ', isPlaying=' .. tostring(playing))
  saveRuntimeReport('Creation mode: ' .. tostring(used) .. '; valid=' .. tostring(valid) .. '; playing=' .. tostring(playing))
end

local function overallResult()
  local streamPresent = probe.runtimeStreamFound or probe.sdkStreamFound
  local audioOK = probe.referenceStatus == 'STARTED_VALID'
  if streamPresent and audioOK then
    return 'RESULT A - PASS: STREAM API EXPOSED + 3D AUDIOEVENT WORKS'
  end
  return 'RESULT B - INCOMPLETE/FAIL: SEE REPORT'
end

scanRuntimeAPI()
readSdkReport()
saveRuntimeReport('App initialized.')

function script.update(dt)
  if probe.referenceEvent then
    local car = ac.getCar(0)
    if car then
      local pos = car.position or vec3(0, 0, 0)
      local vel = car.velocity or vec3(0, 0, 0)
      pcall(function() probe.referenceEvent:setPosition(pos, nil, nil, vel) end)
    end
  end
end

function script.windowMain()
  ui.text('AC SoundSim - PCM / 3D CSP feasibility probe v0.2')
  ui.separator()

  ui.text('APP: LOADED')
  ui.text('STREAM API: ' .. ((probe.runtimeStreamFound or probe.sdkStreamFound) and 'FOUND' or 'NOT FOUND'))
  ui.text('REFERENCE AUDIO: ' .. probe.referenceStatus)

  if probe.referenceEvent then
    local valid, playing = false, false
    pcall(function() valid = probe.referenceEvent:isValid() end)
    pcall(function() playing = probe.referenceEvent:isPlaying() end)
    ui.text('AudioEvent isValid: ' .. tostring(valid))
    ui.text('AudioEvent isPlaying: ' .. tostring(playing))
  end

  ui.separator()
  ui.text(overallResult())
  ui.separator()

  if ui.button('1. Rescan streaming API', vec2(ui.availableSpaceX(), 34)) then
    scanRuntimeAPI()
    readSdkReport()
    saveRuntimeReport('Manual rescan.')
  end

  if ui.button('2. Play reference tone', vec2(ui.availableSpaceX(), 34)) then
    createReferenceEvent()
  end

  if ui.button('3. Write diagnostic report now', vec2(ui.availableSpaceX(), 34)) then
    saveRuntimeReport('Manual report write.')
    ac.setMessage('AC SoundSim Probe', 'probe_runtime_report.txt written in the app folder.')
  end

  if ui.button('Stop reference', vec2(ui.availableSpaceX(), 28)) then
    disposeReference()
    probe.referenceStatus = 'NOT TESTED'
    saveRuntimeReport('Reference stopped.')
  end

  ui.separator()
  ui.text('Reports are in:')
  ui.textWrapped(__dirname)
  ui.text('Files:')
  ui.text('  csp_stream_api_report.txt')
  ui.text('  probe_runtime_report.txt')

  if probe.lastError ~= '' then
    ui.separator()
    ui.text('Last error:')
    ui.textWrapped(probe.lastError)
  end
end

ac.onRelease(function()
  saveRuntimeReport('App released.')
  disposeReference()
end)
