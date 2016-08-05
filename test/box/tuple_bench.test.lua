package.cpath = '../box/?.so;../box/?.dylib;'..package.cpath

net = require('net.box')

c = net:new(os.getenv("LISTEN"))

box.schema.func.create('tuple_bench', {language = "C"})
box.schema.user.grant('guest', 'execute', 'function', 'tuple_bench')
space = box.schema.space.create('tester')
key_parts = {1, 'unsigned', 2, 'string'}
_ = space:create_index('primary', {type = 'TREE', parts =  key_parts})
box.schema.user.grant('guest', 'read,write', 'space', 'tester')

box.space.tester:insert({1, "abc", 100})
box.space.tester:insert({2, "bcd", 200})
box.space.tester:insert({3, "ccd", 200})

prof = require('gperftools.cpu')
prof.start('tuple.prof')

key_types = {}
for i = 1, #key_parts, 2 do table.insert(key_types, key_parts[i + 1]) end  

c:call('tuple_bench', key_types)

prof.flush()
prof.stop()

box.schema.func.drop("tuple_bench")

box.space.tester:drop()
