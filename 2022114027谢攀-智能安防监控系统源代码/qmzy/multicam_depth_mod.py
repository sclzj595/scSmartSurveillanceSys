import cv2

class MultiCamDepthModule:
    def __init__(self):
        self.stereo = cv2.StereoBM_create(numDisparities=16, blockSize=15)

    def run(self):
        capL = cv2.VideoCapture(0)
        capR = cv2.VideoCapture(1)
        while True:
            retL, frameL = capL.read()
            retR, frameR = capR.read()
            if not retL or not retR:
                break
            grayL = cv2.cvtColor(frameL, cv2.COLOR_BGR2GRAY)
            grayR = cv2.cvtColor(frameR, cv2.COLOR_BGR2GRAY)
            disp = self.stereo.compute(grayL, grayR)
            cv2.imshow('Disparity', disp)
            if cv2.waitKey(1) & 0xFF == 27:
                break
        capL.release()
        capR.release()
        cv2.destroyAllWindows()