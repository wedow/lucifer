class Worker:
    def run(self):
        self.step()

    def step(self):
        helper()

    @classmethod
    def make(cls):
        return cls().run()

    @staticmethod
    def build():
        return Worker.make()


def helper():
    raise RuntimeError('in helper')


Worker.build()
