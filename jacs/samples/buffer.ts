const b = buffer(20)

const bufa = hex`00 01 02 03 04`

// comment
function testBuffer() {
    // test cmt
    b.setAt(8, /* cmt */ "u22.10", 173.282)
    const z = b.getAt(4, "u32")

    const idx = 0
    while (idx < 4) {
        b.blitAt(3, bufa, idx, 2)
    }
}

testBuffer()

const dotmatrix = roles.dotMatrix()
dotmatrix.dots.write(hex`00 ab 12 2f 00`)

