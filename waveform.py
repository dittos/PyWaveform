import cwaveform

__version__ = '0.4'

def draw(inAudioFile, outImageFile, (imageWidth, imageHeight), cheat=False):
    """
    Draws the waveform of inAudioFile to picture file outImageFile.
    """
    return cwaveform.draw(inAudioFile, outImageFile, imageWidth, imageHeight, cheat)
