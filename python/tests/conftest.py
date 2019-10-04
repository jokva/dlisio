import pytest

import dlisio

@pytest.fixture(scope="module", name="DWL206")
def DWL206():
    with dlisio.load('data/206_05a-_3_DWL_DWL_WIRE_258276498.DLIS') as (f,):
        yield f


@pytest.fixture(scope="module")
def merge_files_oneLR():
    """
    Merges list of files containing only one LR in one VR.
    Updates VR length and LRS length. See update_envelope_LRSL docs for
    padbytes processing.
    """
    def merge(fpath, flist):
        b = bytearray()
        for file in flist:
            with open(file, 'rb') as source:
                b += bytearray(source.read())

        with open(fpath, "wb") as dest:
            update_envelope_VRL_and_LRSL(b, lrs_offset = 84)
            dest.write(b)

    return merge


@pytest.fixture(scope="module")
def merge_files_manyLR():
    """
    Merges list of files containing one VR. All the files but first one are
    expected to contain one LR with exactly one LRS. For more see
    update_envelope_LRSL docs.
    Updates VR length and LRS lengths. Assures padbytes are added.
    """
    def merge(fpath, flist):
        b = bytearray()

        with open(flist[0], 'rb') as source:
            b += bytearray(source.read())

        for file in flist[1:]:
            with open(file, 'rb') as source:
                b1 = bytearray(source.read())
                update_envelope_LRSL(b1)
                b += b1

        with open(fpath, "wb") as dest:
            update_envelope_VRL_and_LRSL(b)
            dest.write(b)

    return merge

def update_envelope_VRL_and_LRSL(b, lrs_offset = None):
    """
    Having bytes 'b' which represent 1 VR which starts from byte 80 (after SUL),
    updates VR with correct length. If lrs_offset is provied, updates LRSL also
    """
    if lrs_offset:
        update_envelope_LRSL(b, lrs_offset)

    vrl = len(b) - 80
    b[80] = vrl // 256
    b[81] = vrl %  256

def update_envelope_LRSL(b, lrs_offset = 0):
    """
    Having bytes 'b' which represent 1 LRS which starts from byte
    'lrs_offset' updates lrsl with padbytes.
    Bytes requirements:
      - represents one LR with one LRS
      - Starts from LRS header
      - Should not contain LRS trailing length or checksum.
      - If attributes declare padbytes, code doesn't do anything - assumption
        is that padbytes were already correcly added by user. If padbytes are
        not declared in attrs, code is modified to have even number of bytes.
        Note: looks like production code curruntly doesn't have parity check,
        but that's RP66 requirement, so it's attempted to be assured here.
    """
    lrsl = len(b) - lrs_offset
    padbytes_count = max(0, 20 - lrsl)

    attr_offs = lrs_offset + 2
    #assumption: if user declared padbytes they are correct. Otherwise - fix
    if b[attr_offs] % 2 == 0:
        if (len(b) + padbytes_count) % 2 :
            padbytes_count += 1

        if padbytes_count > 0:
            b[attr_offs] = b[attr_offs] + 1
            padbytes = bytearray([0x01] * (padbytes_count - 1))
            padbytes.extend([padbytes_count])
            b.extend(padbytes)

    lrsl = len(b) - lrs_offset
    b[lrs_offset]     = lrsl // 256
    b[lrs_offset + 1] = lrsl %  256


@pytest.fixture
def assert_log(caplog):
    def assert_message(message_id):
        assert any([message_id in r.message for r in caplog.records])
    return assert_message

@pytest.fixture(scope="module")
def fpath(tmpdir_factory, merge_files_manyLR):
    path = str(tmpdir_factory.mktemp('semantic').join('semantic.dlis'))
    content = [
        'data/semantic/envelope.dlis.part',
        'data/semantic/file-header.dlis.part',
        'data/semantic/origin.dlis.part',
        'data/semantic/well-reference-point.dlis.part',
        'data/semantic/axis.dlis.part',
        'data/semantic/long-name-record.dlis.part',
        'data/semantic/channel.dlis.part',
        'data/semantic/frame.dlis.part',
        'data/semantic/fdata-frame1-1.dlis.part',
        'data/semantic/fdata-frame1-2.dlis.part',
        'data/semantic/fdata-frame1-3.dlis.part',
        'data/semantic/path.dlis.part',
        'data/semantic/zone.dlis.part',
        'data/semantic/parameter.dlis.part',
        'data/semantic/equipment.dlis.part',
        'data/semantic/tool.dlis.part',
        'data/semantic/process.dlis.part',
        'data/semantic/computation.dlis.part',
        'data/semantic/measurement.dlis.part',
        'data/semantic/coefficient.dlis.part',
        'data/semantic/coefficient-wrong.dlis.part',
        'data/semantic/calibration.dlis.part',
        'data/semantic/group.dlis.part',
        'data/semantic/splice.dlis.part',
        'data/semantic/message.dlis.part',
        'data/semantic/comment.dlis.part',
        'data/semantic/update.dlis.part',
        'data/semantic/unknown.dlis.part',
    ]
    merge_files_manyLR(path, content)
    return path

@pytest.fixture(scope="module")
def f(fpath):
    with dlisio.load(fpath) as (f, *_):
        yield f