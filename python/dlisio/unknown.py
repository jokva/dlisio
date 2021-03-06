from .basic_object import basic_object


class Unknown(basic_object):
    """
    The unknown object implements a dict interface and is intended as a
    fall-back object if the object-type is not recognized by dlisio, e.g.
    vendor spesific object types
    """
    def __init__(self, obj):
        super().__init__(obj, "unknown")
        self.attributes = {a.label.lower() : a.value for a in obj.values()}
        self.stripspaces()

    def __getattr__(self, key):
        return self.attributes[key]

    def __str__(self):
        s  = "dlisio.unknown:\n"
        s += "\tname: {}\n".format(self.name)
        s += "\ttype: {}\n".format(self.type)
        for key, value in self.attributes.items():
            s += "\t{}: {}\n".format(key, value)
        s += "\tattic: {}\n".format(self.type)
        return s
