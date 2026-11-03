class Silent(Exception):
    def __str__(self):
        return ''


raise Silent('hidden text')
