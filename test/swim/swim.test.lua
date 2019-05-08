test_run = require('test_run').new()
test_run:cmd("push filter '\\.lua.*:[0-9]+: ' to '.lua:<line>: '")
test_run:cmd("push filter '127.0.0.1:[0-9]+$' to '127.0.0.1:<port>'")
msgpack = require('msgpack')
ffi = require('ffi')
--
-- gh-3234: SWIM gossip protocol.
--

-- Invalid cfg parameters.
swim.new(1)
swim.new({uri = true})
swim.new({heartbeat_rate = 'rate'})
swim.new({ack_timeout = 'timeout'})
swim.new({gc_mode = 'not a mode'})
swim.new({gc_mode = 0})
swim.new({uuid = 123})
swim.new({uuid = '1234'})

-- Valid parameters, but invalid configuration.
swim.new({})
swim.new({uuid = uuid(1)})
swim.new({uri = uri()})

-- Check manual deletion.
s = swim.new({uuid = uuid(1), uri = uri()})
s:delete()
s:cfg({})
s = nil
_ = collectgarbage('collect')

s = swim.new({uuid = uuid(1), uri = uri()})
s:quit()
s:is_configured()
s = nil
_ = collectgarbage('collect')

s = swim.new({uuid = uuid(1), uri = uri()})
s:is_configured()
s:size()
s.cfg
s.cfg.gc_mode = 'off'
s:cfg{gc_mode = 'off'}
s.cfg
s.cfg.gc_mode
s.cfg.uuid
s.cfg()
s:cfg({wrong_opt = 100})
s:delete()

-- Reconfigure.
s = swim.new()
s:is_configured()
-- Check that not configured instance does not provide most of
-- methods.
s.quit == nil
s:cfg({uuid = uuid(1), uri = uri()})
s.quit ~= nil
s:is_configured()
s:size()
s = nil
_ = collectgarbage('collect')

-- Invalid usage.
s = swim.new()
s.delete()
s.is_configured()
s.cfg()
s:delete()

--
-- Basic member table manipulations.
--
s1 = swim.new({uuid = uuid(1), uri = uri(), heartbeat_rate = 0.01})
s2 = swim.new({uuid = uuid(2), uri = listen_uri, heartbeat_rate = 0.01})

s1.broadcast()
s1:broadcast('wrong port')
-- Note, broadcast takes a port, not a URI.
s1:broadcast('127.0.0.1:3333')
-- Ok to broadcast on default port.
s1:broadcast()

s1:broadcast(listen_port)
while s2:size() ~= 2 do fiber.sleep(0.01) end
s1:size()
s2:size()

s1.remove_member()
s1:remove_member(100)
s1:remove_member('1234')
s1:remove_member(uuid(2))
s1:size()

s1.add_member()
s1:add_member(100)
s1:add_member({uri = true})
s1:add_member({uri = listen_uri})
s1:add_member({uuid = uuid(2)})
s1:add_member({uri = listen_uri, uuid = uuid(2)})
s1:add_member({uri = listen_uri, uuid = uuid(2)})
s1:size()

s1:cfg({uuid = uuid(3)})
s1:self():uuid()
s1:member_by_uuid(uuid(1))
-- Can't remove self.
s1:remove_member(uuid(3))
-- Not existing.
s1:remove_member(uuid(4))
-- Old self.
s1:remove_member(uuid(1))

s1:delete()
s2:delete()

s1 = swim.new({uuid = uuid(1), uri = uri()})
s2 = swim.new({uuid = uuid(2), uri = listen_uri})
s1.probe_member()
s1:probe_member()
s1:probe_member(true)
-- Not existing URI is ok - nothing happens.
s1:probe_member('127.0.0.1:1')
fiber.yield()
s1:size()
s1:probe_member(listen_uri)
while s1:size() ~= 2 do fiber.sleep(0.01) end
s2:size()

s1:delete()
s2:delete()

--
-- Member API.
--
s1 = swim.new({uuid = uuid(1), uri = uri()})
s = s1:self()
s
s:status()
s:uuid()
s:uri()
s:incarnation()
s:payload_cdata()
s:payload_str()
s:payload()
s:is_dropped()
s.unknown_index

s.status()
s.uuid()
s.uri()
s.incarnation()
s.payload_cdata()
s.payload_str()
s.payload()
s.is_dropped()

s1:member_by_uuid(uuid(1)) ~= nil
s1:member_by_uuid(50)
s1:member_by_uuid(uuid(2))

s1:quit()
s:status()
s:is_dropped()

--
-- Payload.
--
s = swim.new({uuid = uuid(1), uri = uri()})
s.set_payload()
s.set_payload_raw()

self = s:self()
s:set_payload()
self:payload()
s:set_payload({a = 100})
self:payload()
s:set_payload(100)
self:payload()
s:set_payload(false)
self:payload()

p = self:payload_str()
p
(msgpack.decode(p))

p, size = self:payload_cdata()
type(p)
size
(msgpack.decode(p, size))

s:set_payload(string.rep('a', 1500))
self:payload()
s:set_payload()
self:payload()

s:set_payload(100)
self:payload()
s:set_payload_raw()
self:payload()

-- Raw payload setting can be used when MessagePack is not needed,
-- or already encoded.
s:set_payload_raw(nil, '123')
s:set_payload_raw('123', -1)

size = 10
cdata = ffi.new('int[?]', size)
for i = 0, size - 1 do cdata[i] = i end
bsize = ffi.sizeof('int') * size
s:set_payload_raw(cdata)
s:set_payload_raw('str', 4)
s:set_payload_raw(true)

s:set_payload_raw(cdata, bsize)
self:payload_str():len() == bsize
self_cdata, self_bsize = self:payload_cdata()
self_bsize == bsize
self_cdata = ffi.cast('int *', self_cdata)
for i = 0, size - 1 do assert(self_cdata[i] == cdata[i]) end

s:set_payload_raw('raw str')
self:payload_str()
s:set_payload_raw('raw str', 3)
self:payload_str()

s:delete()
self:is_dropped()

--
-- Check payload dissemination.
--
s1 = swim.new({uuid = uuid(1), uri = uri(), heartbeat_rate = 0.01})
s2 = swim.new({uuid = uuid(2), uri = listen_port, heartbeat_rate = 0.01})
s1:add_member({uuid = uuid(2), uri = listen_port})
while s2:size() ~= 2 do fiber.sleep(0.01) end
s1_view = s2:member_by_uuid(uuid(1))
s1_view:payload()
s1_view:incarnation()

s1:set_payload('payload')
while s1_view:payload() ~= 'payload' do fiber.sleep(0.01) end
s1_view:incarnation()

s1:set_payload('payload2')
while s1_view:payload() ~= 'payload2' do fiber.sleep(0.01) end
s1_view:incarnation()

s1:delete()
s2:delete()

test_run:cmd("clear filter")
