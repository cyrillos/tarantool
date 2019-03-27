test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
box.sql.execute('pragma sql_default_engine=\''..engine..'\'')

box.cfg{}

box.sql.execute("select (9223372036854775807)")
box.sql.execute("select (-9223372036854775808)")

box.sql.execute("select (9223372036854775808)")
box.sql.execute("select (-9223372036854775809)")

-- cause an overflow
box.sql.execute("select (92233720368547758080)")
box.sql.execute("select (-92233720368547758090)")
