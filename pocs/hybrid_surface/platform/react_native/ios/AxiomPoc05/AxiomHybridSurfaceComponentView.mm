#import "AxiomHybridSurfaceComponentView.h"

#import "AxiomAppleHybridSurfaceHost.h"
#import <react/renderer/components/AxiomPoc05Spec/ComponentDescriptors.h>
#import <react/renderer/components/AxiomPoc05Spec/Props.h>

@implementation AxiomHybridSurfaceComponentView {
  AxiomAppleHybridSurfaceHost* _host;
}

+ (facebook::react::ComponentDescriptorProvider)componentDescriptorProvider {
  return facebook::react::concreteComponentDescriptorProvider<
      facebook::react::AxiomHybridSurfaceComponentDescriptor>();
}

- (instancetype)initWithFrame:(CGRect)frame {
  if ((self = [super initWithFrame:frame])) {
    _host = [[AxiomAppleHybridSurfaceHost alloc] initWithFrame:self.bounds];
    _host.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                             UIViewAutoresizingFlexibleHeight;
    self.contentView = _host;
  }
  return self;
}

- (void)updateProps:(const facebook::react::Props::Shared&)props
           oldProps:(const facebook::react::Props::Shared&)oldProps {
  [super updateProps:props oldProps:oldProps];
  const auto& next = *std::static_pointer_cast<const facebook::react::AxiomHybridSurfaceProps>(props);
  [_host setWebVisible:next.webVisible];
  [_host setFailureMode:next.failureMode];
  [_host setActivePage:next.activePage];
  [_host setLifecycleGeneration:next.lifecycleGeneration];
}

- (void)prepareForRecycle {
  [_host setWebVisible:NO];
  [_host setActivePage:2];
  [super prepareForRecycle];
}

@end
