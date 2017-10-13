#include "ColliderSpace.h"

#include "../gameobject/components/ColliderAABBComponent.h"
#include "../gameobject/components/RigidBodyComponent.h"
#include "../gameobject/components/TransformComponent.h"
#include "../gameobject/GameObject.h"

#include <algorithm>
#include <cassert>

Raycast::Raycast() : Raycast({0, 0}, {1, 0}) {}

Raycast::Raycast(sf::Vector2f const& start, sf::Vector2f const& direction, float distance) 
    : start(start), direction(direction), distance(distance) {}

bool Raycast::is_infinite() const {
    return distance >= Raycast::max_distance;
}

sf::Vector2f Raycast::at(float d) const {
    return start + direction * d;
}

float Raycast::at_x(float d) const {
    return start.x + direction.x * d;
}

float Raycast::at_y(float d) const {
    return start.y + direction.y * d;
}

RaycastInfo::RaycastInfo() : RaycastInfo(nullptr, 0) {}

RaycastInfo::RaycastInfo(TransformComponent* other, float distance) 
    : other(other), distance(distance) {}

RaycastInfo::operator bool() const {
    return other != nullptr;
}

bool ColliderOwner::operator != (ColliderOwner const& o) const {
    return !(*this == o);
}

bool ColliderOwner::operator == (ColliderOwner const& o) const {
    return tf == o.tf || rb == o.rb;
}

void ColliderSpace::insert(ColliderOwner const& collider) {
    m_colliders.push_back(collider);
}

void ColliderSpace::remove(ColliderOwner const& collider) {
    m_colliders.erase(std::remove(m_colliders.begin(), m_colliders.end(), collider), m_colliders.end());
}

void ColliderSpace::remove(TransformComponent* tf) {
    remove({tf, nullptr, nullptr});
}

void ColliderSpace::updateRigidBody(TransformComponent* tf, RigidBodyComponent* rb) {
    for(auto& c : m_colliders) {
        if (c.tf == tf) {
            c.rb = rb;
            break;
        }
    }
}

void ColliderSpace::update(sf::Time const& time)
{
    for (auto it0 = m_colliders.begin(); it0 != m_colliders.end(); ++it0) 
        for (auto it1 = it0 + 1; it1 != m_colliders.end(); ++it1) 
            checkCollision(*it0, *it1);
}

RaycastInfo ColliderSpace::raycast(Raycast const& r, std::unordered_set<TransformComponent*> const& ignored) {
    RaycastInfo info(nullptr, r.distance);
    for (auto& c : m_colliders) 
    {
        if (ignored.find(c.tf) == ignored.end())
        {
            checkRaycastCollision(r, c, info);
            if (r.distance == 0)
                return info;
        }
    }
    return info;
}

void ColliderSpace::resolveCollision(ColliderOwner& c0, ColliderOwner& c1, sf::Vector2f const& direction)
{
    auto rb0 = c0.rb;
    auto rb1 = c1.rb;
    if (rb0 && rb1) 
    {
        float tot = rb0->inv_mass + rb1->inv_mass;
        float f0 = rb0->inv_mass / tot;
        float f1 = rb1->inv_mass / tot;
        
        constexpr float epsilon = 0.01; 
        assert(f0 + f1 > 1 - epsilon || f0 + f1 < 1 + epsilon);

        c0.tf->position += direction * f0;
        c1.tf->position -= direction * f1;
        // event
        CollisionInfo ci0 {c1.tf, c1.collider, direction * f0};
        CollisionInfo ci1 {c0.tf, c0.collider, direction * f1};
        c0.onCollision(ci0);
        c1.onCollision(ci1);
    }
    TriggerInfo t0 {c1.tf, c1.collider};
    TriggerInfo t1 {c0.tf, c0.collider};
    c0.onTrigger(t0);
    c1.onTrigger(t1);
}

void ColliderSpace::checkRaycastCollision(Raycast const& r, ColliderOwner& co, RaycastInfo& info) {
    sf::Vector2f normal(r.direction.y, -r.direction.x);
    Projection proj = co.collider->project(normal, co.tf->position);
    float rayproj = r.start.x * normal.x + r.start.y * normal.y;
    if (proj.first <= rayproj && rayproj <= proj.second) {
 
        auto pts = co.collider->getPoints(co.tf->position);
        for (int i = 0; i < pts.size(); i++) {
            sf::Vector2f c = pts[i];
            sf::Vector2f m = pts[(i+1)%pts.size()] - c;
            float d = r.direction.y * m.x - r.direction.x * m.y;
            if (d != 0) {
                float n = ((r.start.x - c.x) * r.direction.y - (r.start.y - c.y) * r.direction.x) / d;
                if (n >= 0 && n <= 1) {
                    sf::Vector2f intersection = c + m * n;
                    float ts;
                    if (r.direction.y == 0)
                        ts = (intersection.x - r.start.x) / r.direction.x;
                    else
                        ts = (intersection.y - r.start.y) / r.direction.y;
                    if (ts >= 0 && ts <= r.distance) {
                        info.other = co.tf;
                        info.distance = ts;
                        return;
                    }
                }
            }
        }
 
    }
}

void ColliderSpace::checkCollision(ColliderOwner& c0, ColliderOwner& c1) {
    std::vector<sf::Vector2f> normals; // remove parallele normal ? with an unordered set ? (but sf::Vectorf doesn't implement a hash's function if i'm right)
    auto n0 = c0.collider->getNormals();
    auto n1 = c1.collider->getNormals();
    normals.reserve(n0.size() + n1.size());
    for (auto& n : n0) normals.push_back(n);
    for (auto& n : n1) normals.push_back(n);

    float min_dist = std::numeric_limits<float>::max();
    sf::Vector2f min_axis;

    for (auto const& n : normals) {
        Projection p0 = c0.collider->project(n, c0.tf->position);
        Projection p1 = c1.collider->project(n, c1.tf->position);

        if (!Projection::overlap(p0, p1))
            return;
        float mtv = getMTV(p0, p1);
        if (std::abs(mtv) < min_dist) min_axis = mtv < 0 ? -n : n, min_dist = std::abs(mtv);
    }
    resolveCollision(c0, c1, min_axis);
}

float ColliderSpace::getMTV(Projection const& p0, Projection const& p1) const {
    // containment
    if (p0.first < p1.first && p0.second > p1.second)
        return std::min(p1.second - p0.first, p0.second - p1.first);
    if (p0.first > p1.first && p0.second > p1.second)
        return std::min(p0.second - p1.first, p1.second - p0.first);

    if (p0.first > p1.first)
        return p0.second - p1.first;
    return p1.first - p0.second;
}