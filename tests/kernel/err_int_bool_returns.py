print('before')
class Bad:
    def __bool__(self):
        return 1
bool(Bad())
