import type {HostComponent, ViewProps} from 'react-native';
import type {
  Int32,
  WithDefault,
} from 'react-native/Libraries/Types/CodegenTypes';
import codegenNativeComponent from 'react-native/Libraries/Utilities/codegenNativeComponent';

export interface NativeProps extends ViewProps {
  webVisible?: WithDefault<boolean, true>;
  failureMode?: WithDefault<boolean, false>;
  activePage?: WithDefault<Int32, 1>;
  lifecycleGeneration?: WithDefault<Int32, 1>;
}

export default codegenNativeComponent<NativeProps>(
  'AxiomHybridSurface',
) as HostComponent<NativeProps>;
