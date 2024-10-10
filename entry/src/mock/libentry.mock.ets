import { resourceManager } from '@kit.LocalizationKit';

const NativeMock: Record<string, Object> = {
  'add': (a: number, b: number) => {
    return a + b;
  },
  'init': (resmgr : resourceManager.ResourceManager) => {
  },
  'start': (id:string) => {
  },
  'show': (id:string) => {
  },
  'hide': (id:string) => {
  },
  'update': (id:string) => {
  },
  'stop': (id:string) => {
  },
};

export default NativeMock;