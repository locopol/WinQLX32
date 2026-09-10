'''
Redis Wrapper to translate calls to Shelve model
'''
VERSION = (2, 10, 6)

class RedisError(Exception):
    pass

class ConnectionError(RedisError):
    pass