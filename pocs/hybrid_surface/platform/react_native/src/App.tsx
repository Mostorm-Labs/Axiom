import React, {useEffect, useState} from 'react';
import {
  Button,
  NativeModules,
  SafeAreaView,
  StatusBar,
  StyleSheet,
  Text,
  View,
} from 'react-native';

import AxiomHybridSurface from './specs/NativeAxiomHybridSurface';

function blockJavaScript(milliseconds: number): void {
  const end = Date.now() + milliseconds;
  while (Date.now() < end) {
    // Intentional POC corpus: Canvas input/render/placement must stay native.
  }
}

export default function App(): React.JSX.Element {
  const [webVisible, setWebVisible] = useState(true);
  const [failureMode, setFailureMode] = useState(false);
  const [activePage, setActivePage] = useState(1);
  const [generation, setGeneration] = useState(1);

  useEffect(() => {
    const timer = setTimeout(() => {
      NativeModules.AxiomPoc05Probe?.beginJsStall(100);
      blockJavaScript(100);
      NativeModules.AxiomPoc05Probe?.endJsStall();
    }, 1200);
    return () => clearTimeout(timer);
  }, []);

  return (
    <SafeAreaView style={styles.root}>
      <StatusBar hidden />
      <View style={styles.toolbar}>
        <Text style={styles.title}>Axiom POC-05 · RN/Fabric Shell</Text>
        <Button
          title={webVisible ? 'Hide Web' : 'Show Web'}
          onPress={() => setWebVisible(value => !value)}
        />
        <Button
          title={failureMode ? 'Recover' : 'Fail Web'}
          onPress={() => setFailureMode(value => !value)}
        />
        <Button
          title={activePage === 1 ? 'Page 2' : 'Page 1'}
          onPress={() => setActivePage(page => (page === 1 ? 2 : 1))}
        />
        <Button
          title="Recreate"
          onPress={() => setGeneration(value => value + 1)}
        />
        <Button
          title="Block JS 100ms"
          onPress={() => {
            NativeModules.AxiomPoc05Probe?.beginJsStall(100);
            blockJavaScript(100);
            NativeModules.AxiomPoc05Probe?.endJsStall();
          }}
        />
      </View>
      <AxiomHybridSurface
        style={styles.canvas}
        webVisible={webVisible}
        failureMode={failureMode}
        activePage={activePage}
        lifecycleGeneration={generation}
      />
    </SafeAreaView>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, backgroundColor: '#101419'},
  toolbar: {
    minHeight: 54,
    paddingHorizontal: 10,
    backgroundColor: '#f8fafc',
    flexDirection: 'row',
    alignItems: 'center',
    gap: 6,
  },
  title: {fontWeight: '700', color: '#172033', marginRight: 8},
  canvas: {flex: 1},
});
