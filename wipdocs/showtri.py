import svgwrite
# from svgwrite import cm, mm   

dwg = svgwrite.Drawing('test.svg', profile='tiny')

height=20
width=17
xstart = 0
ystart = 0
xstop = 0
ystop = 0
marg = 2
scale = 32

def set_grid(min, max):
    xmin, ymin = min
    xmax, ymax = max

    global height, width
    height = ymax - ymin
    width = xmax - xmin
    global xstart, ystart, xstop, ystop
    xstart = xmin
    ystart = ymin
    xstop = xmax
    ystop = ymax

def coord(x,y):
    """Map pixel coords to canvas coords"""
    return (marg+x-xstart)*scale, (marg+y-ystart)*scale

def tricoord(tri):
    return [coord(x,y) for x,y in tri]

def draw_grid():
    hlines = dwg.add(dwg.g(id='hlines', stroke='green'))
    for y in range(ystart,ystop+1):
        # hlines.add(dwg.line(start=(marg, marg+y*cm), end=(width*cm + marg, marg+y*cm)))
        hlines.add(dwg.line(start=coord(xstart, y), end=coord(xstop, y)))
    vlines = dwg.add(dwg.g(id='vline', stroke='blue'))

    for x in range(xstart,xstop+1):
        vlines.add(dwg.line(start=coord(x,ystart), end=coord(x, ystop)))

    dwg.add(dwg.line((0, 0), (10, 0), stroke=svgwrite.rgb(10, 10, 16, '%')))
    for x in range(xstart, xstop):
        dwg.add(dwg.text(x, insert=coord(x+0.30, ystart-0.25), fill='black'))
    for y in range(ystart, ystop):
        dwg.add(dwg.text(y, insert=coord(xstart-0.75, y+0.7), fill='black'))

def draw_tri(tri):
    dwg.add(dwg.polygon(tricoord(tri), fill_opacity='0.25', stroke='black'))

# set_grid(min=(6, 10), max=(16,17))
# draw_grid()
# draw_tri(((7.5, 12.5), (12, 15.1), (9.2, 11.2)))

set_grid(min=(30, 22), max=(37, 32))
draw_grid()
draw_tri([(30.968006, 31.061892), (34.444366, 27.504200), (36.917759, 22.824471)])


dwg.save()