print('before')
class Bad:
    def __len__(self):
        return -1
bool(Bad())
