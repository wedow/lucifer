import abc


class Root(abc.ABC):
    pass


class RegistrationMeta(type):
    def __hash__(cls):
        if cls.block_hash:
            raise RuntimeError('registered class was hashed again')
        return type.__hash__(cls)


class Registered(metaclass=RegistrationMeta):
    block_hash = False


class Child(Registered):
    block_hash = False


class Unrelated:
    pass


assert not isinstance(Child(), Root)
token = abc.get_cache_token()
assert Root.register(Registered) is Registered
assert abc.get_cache_token() != token

# Recursive checks should walk the stored weak references without rehashing
# the registered classes. Check the cold path, then both cache hit paths.
Registered.block_hash = True
for _ in range(3):
    assert isinstance(Child(), Root)
    assert issubclass(Child, Root)
    assert not isinstance(Unrelated(), Root)

token = abc.get_cache_token()
assert Root.register(Child) is Child
assert abc.get_cache_token() == token
print('registry and cache checks passed')
