def work():
    try:
        raise LookupError('body')
    finally:
        print('cleanup')


work()
